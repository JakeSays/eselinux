// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Process-global io_uring + a completion thread.
//
// Design (chosen 2026-05-07):
//   - One io_uring instance shared by every CIoUringFile in the process.
//     liburing is set up at OS-layer init and torn down at OS-layer term.
//   - A dedicated completion thread loops on io_uring_wait_cqe and
//     dispatches each CQE to the IOContext referenced via user_data.
//   - Each in-flight IO carries an IOContext heap-allocated by the
//     submitter. The IOContext owns:
//       * the engine PfnIOComplete callback + args
//       * a pthread_mutex/cond pair used by the synchronous-API path
//         (pfnIOComplete == nullptr) so the submitting thread blocks
//         until the completion thread signals done
//   - The "async" path returns immediately; the completion thread fires
//     PfnIOComplete and frees the IOContext.
//
// Submission is serialized through a single mutex covering the SQ. The
// io_uring SQ is fast enough that this isn't a bottleneck for the
// concurrency levels ESE actually runs (a few dozen in-flight IOs).
//
// Mapping: io_uring's CQE.res < 0 -> JET_errDiskIO; res != cbData ->
// JET_errFileIOBeyondEOF (read short) / JET_errDiskFull (write short).

#include "osstd.hxx"
#include "iouring_posix.hxx"

//  windows-shim/specstrings.h #defines __reserved → empty as a SAL
//  annotation. liburing.h transitively pulls <linux/fscrypt.h>, which
//  has a struct field literally named __reserved. Drop the macro just
//  before liburing comes in.
#undef __reserved
#include <liburing.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <atomic>

namespace osposix
{

namespace
{
    constexpr unsigned int c_ringDepth = 256;

    struct GlobalRing
    {
        io_uring        ring;
        pthread_mutex_t submitMutex;
        pthread_t       completionThread;
        std::atomic<bool> shuttingDown;
        bool            inited;
    };

    GlobalRing g_ring;

    void DispatchCompletion( IOContext* ctx, int cqeRes )
    {
        ERR err = JET_errSuccess;
        if ( cqeRes < 0 )
        {
            err = ErrERRCheck( JET_errDiskIO );
        }
        else if ( ctx->op == IOContext::Op::Read && (DWORD)cqeRes < ctx->cbData )
        {
            err = ErrERRCheck( JET_errFileIOBeyondEOF );
        }
        else if ( ctx->op == IOContext::Op::Write && (DWORD)cqeRes < ctx->cbData )
        {
            err = ErrERRCheck( JET_errDiskFull );
        }

        if ( ctx->pfnIOComplete )
        {
            //  Async path: completion thread invokes the callback and
            //  releases the context.
            FullTraceContext ftc;
            ftc.etc = ctx->tc;
            ctx->pfnIOComplete( err, ctx->fapi, ftc, ctx->qos,
                                ctx->ibOffset, ctx->cbData, ctx->pbData,
                                ctx->keyIOComplete );
            delete ctx;
        }
        else
        {
            //  Sync path: stash the result and wake the submitting thread.
            pthread_mutex_lock( &ctx->lock );
            ctx->res  = err;
            ctx->done = true;
            pthread_cond_signal( &ctx->cond );
            pthread_mutex_unlock( &ctx->lock );
            //  Submitter owns the ctx — must NOT delete here.
        }
    }

    void* CompletionLoop( void* /*unused*/ )
    {
        while ( !g_ring.shuttingDown.load( std::memory_order_acquire ) )
        {
            io_uring_cqe* cqe = nullptr;
            const int rc = io_uring_wait_cqe( &g_ring.ring, &cqe );
            if ( rc == -EINTR )
            {
                continue;
            }
            if ( rc < 0 || !cqe )
            {
                //  Ring is being torn down or wedged; bail.
                break;
            }

            //  user_data == 0 marks the wakeup nop submitted by Term.
            IOContext* const ctx = reinterpret_cast< IOContext* >( io_uring_cqe_get_data( cqe ) );
            const int res = cqe->res;
            io_uring_cqe_seen( &g_ring.ring, cqe );

            if ( ctx )
            {
                DispatchCompletion( ctx, res );
            }
        }
        return nullptr;
    }

}  // namespace

namespace
{
    pthread_once_t g_iouringOnce = PTHREAD_ONCE_INIT;
    ERR            g_iouringInitErr = JET_errSuccess;

    void IOUringInitOnce()
    {
        pthread_mutex_init( &g_ring.submitMutex, nullptr );
        g_ring.shuttingDown.store( false, std::memory_order_release );

        const int rc = io_uring_queue_init( c_ringDepth, &g_ring.ring, 0 );
        if ( rc < 0 )
        {
            pthread_mutex_destroy( &g_ring.submitMutex );
            g_iouringInitErr = ErrERRCheck( JET_errOutOfMemory );
            return;
        }

        if ( pthread_create( &g_ring.completionThread, nullptr, CompletionLoop, nullptr ) != 0 )
        {
            io_uring_queue_exit( &g_ring.ring );
            pthread_mutex_destroy( &g_ring.submitMutex );
            g_iouringInitErr = ErrERRCheck( JET_errOutOfMemory );
            return;
        }

        g_ring.inited = true;
    }
}

//  Lazy bring-up — first call to any submission entry point fires the
//  init once.  Keeps the io_uring lifecycle off the OS-layer init hook
//  list so we don't need upstream osfile.cxx changes to drive it.
ERR ErrIOUringInit()
{
    pthread_once( &g_iouringOnce, IOUringInitOnce );
    return g_iouringInitErr;
}

void IOUringTerm()
{
    if ( !g_ring.inited )
    {
        return;
    }

    g_ring.shuttingDown.store( true, std::memory_order_release );

    //  Wake the completion thread by submitting a no-op SQE with
    //  user_data == 0 so the dispatcher recognizes it as a poison pill.
    pthread_mutex_lock( &g_ring.submitMutex );
    io_uring_sqe* sqe = io_uring_get_sqe( &g_ring.ring );
    if ( sqe )
    {
        io_uring_prep_nop( sqe );
        io_uring_sqe_set_data( sqe, nullptr );
        io_uring_submit( &g_ring.ring );
    }
    pthread_mutex_unlock( &g_ring.submitMutex );

    pthread_join( g_ring.completionThread, nullptr );
    io_uring_queue_exit( &g_ring.ring );
    pthread_mutex_destroy( &g_ring.submitMutex );
    g_ring.inited = false;
}

namespace
{
    void InitContextSyncWait( IOContext* ctx )
    {
        pthread_mutex_init( &ctx->lock, nullptr );
        pthread_cond_init( &ctx->cond, nullptr );
        ctx->done = false;
        ctx->res  = 0;
    }

    ERR SubmitAndMaybeWait( IOContext* ctx,
                            void ( *prep )( io_uring_sqe*, IOContext* ) )
    {
        pthread_mutex_lock( &g_ring.submitMutex );
        io_uring_sqe* sqe = io_uring_get_sqe( &g_ring.ring );
        if ( !sqe )
        {
            pthread_mutex_unlock( &g_ring.submitMutex );
            //  Ring full — rare under our concurrency. Caller should
            //  retry; for now return errOutOfMemory.
            return ErrERRCheck( JET_errOutOfMemory );
        }
        prep( sqe, ctx );
        io_uring_sqe_set_data( sqe, ctx );
        const int rc = io_uring_submit( &g_ring.ring );
        pthread_mutex_unlock( &g_ring.submitMutex );

        if ( rc < 0 )
        {
            return ErrERRCheck( JET_errDiskIO );
        }

        if ( ctx->pfnIOComplete )
        {
            //  Async — the completion thread will invoke the callback
            //  and free ctx. Engine convention: ErrIORead/Write returns
            //  errSuccess from the async path; the actual disposition
            //  arrives via pfnIOComplete.
            return JET_errSuccess;
        }

        //  Sync — block on the IOContext's cond.
        pthread_mutex_lock( &ctx->lock );
        while ( !ctx->done )
        {
            pthread_cond_wait( &ctx->cond, &ctx->lock );
        }
        const int res = ctx->res;
        pthread_mutex_unlock( &ctx->lock );
        return (ERR)res;
    }
}

ERR ErrIOUringRead( int fd, IOContext* ctx )
{
    const ERR errInit = ErrIOUringInit();
    if ( errInit < JET_errSuccess )
    {
        return errInit;
    }
    if ( !ctx->pfnIOComplete )
    {
        InitContextSyncWait( ctx );
    }
    return SubmitAndMaybeWait( ctx,
        []( io_uring_sqe* sqe, IOContext* ctx )
        {
            io_uring_prep_read( sqe, ctx->fileFd, ctx->pbData,
                                ctx->cbData, ctx->ibOffset );
        } );
}

ERR ErrIOUringWrite( int fd, IOContext* ctx )
{
    const ERR errInit = ErrIOUringInit();
    if ( errInit < JET_errSuccess )
    {
        return errInit;
    }
    if ( !ctx->pfnIOComplete )
    {
        InitContextSyncWait( ctx );
    }
    return SubmitAndMaybeWait( ctx,
        []( io_uring_sqe* sqe, IOContext* ctx )
        {
            io_uring_prep_write( sqe, ctx->fileFd, ctx->pbData,
                                 ctx->cbData, ctx->ibOffset );
        } );
}

ERR ErrIOUringFsync( int fd )
{
    const ERR errInit = ErrIOUringInit();
    if ( errInit < JET_errSuccess )
    {
        return errInit;
    }
    //  Synchronous fsync via the ring keeps every IO path going
    //  through one syscall surface (helpful for tracing / future
    //  io_uring_register_files).
    IOContext ctx{};
    ctx.op             = IOContext::Op::Fsync;
    ctx.fileFd         = fd;
    InitContextSyncWait( &ctx );
    const ERR err = SubmitAndMaybeWait( &ctx,
        []( io_uring_sqe* sqe, IOContext* c )
        {
            io_uring_prep_fsync( sqe, c->fileFd, 0 );
        } );
    pthread_mutex_destroy( &ctx.lock );
    pthread_cond_destroy( &ctx.cond );
    return err;
}

void DestroyContextSyncWait( IOContext* ctx )
{
    pthread_mutex_destroy( &ctx->lock );
    pthread_cond_destroy( &ctx->cond );
}

}  // namespace osposix
