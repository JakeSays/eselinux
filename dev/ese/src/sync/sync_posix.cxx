// POSIX equivalent of the Win32 lower half of sync.cxx (everything below the
// `#ifdef _WIN32` gate at line ~2899). Provides the OSSYNC namespace's
// OS-dependent surface: page memory, kernel semaphores, processor info,
// per-context (CLS) and per-processor (PLS) local storage, the tick-time
// helper, thread-wait callbacks, deadlock-detection toggle, and the sync
// subsystem lifecycle (FOSSyncPreinit / OSSyncPostterm / OSSyncProcessAbort).
//
// This file is the link-clean Phase 5 implementation. The non-DEBUG build
// disables SYNC_ENHANCED_STATE / SYNC_DEADLOCK_DETECTION /
// SYNC_ANALYZE_PERFORMANCE / SYNC_DUMP_PERF_DATA so the corresponding
// allocator + dump scaffolding from upstream is intentionally not ported.
//
// Mappings:
//   VirtualAlloc/VirtualFree   -> mmap / mprotect / madvise / munmap
//   CreateSemaphoreW           -> sem_t on the heap (m_handle = sem_t*)
//   WaitForSingleObjectEx      -> sem_wait / sem_trywait / sem_timedwait
//   ReleaseSemaphore           -> sem_post (loop for cToRelease > 1)
//   GetTickCount               -> clock_gettime(CLOCK_MONOTONIC) -> ms
//   TlsAlloc/TlsGetValue/...   -> pthread_key_create / pthread_*specific
//   GetThreadIdealProcessorEx  -> sched_getcpu()
//   GetLogicalProcessorInfo... -> sysconf(_SC_NPROCESSORS_ONLN)
//   Sleep( 0 )                 -> sched_yield()

#include "sync.hxx"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <mutex>

namespace OSSYNC {


//  Global Synchronization Constants

//  Win32 INFINITE is 0xFFFFFFFF; in INT it's -1. We hardcode here because the
//  sync target compiles without the windows-shim on its include path.
const INT cmsecTest                 = 0;
const INT cmsecInfinite             = (INT)0xFFFFFFFF;
const INT cmsecDeadlock             = 600000;       // 10 min
const INT cmsecInfiniteNoDeadlock   = (INT)0xFFFFFFFE;

//  Cache line size — must match upstream (32). Several engine invariants are
//  built around this value, e.g. cresmgr.cxx EnforceSz that cbRESHeader (32)
//  divides evenly by cbCacheLine. The actual x86_64 L1 line is 64 bytes, but
//  changing this breaks the resource-manager layout so we keep the upstream
//  number and live with the (negligible) extra cache-line crossings.
const INT cbCacheLine               = 32;

//  Globals shared with the upper (portable) half of sync.cxx —
//  g_cSpinMax / g_fSyncProcessAbort / g_sdltosState are defined there.
extern INT      g_cSpinMax;
extern BOOL     g_fSyncProcessAbort;

//  Processor topology — Win32 splits cores into groups of <=64 (KAFFINITY
//  bitmap), Linux exposes a flat space. Defined here because the upstream
//  definitions are inside the `#ifdef _WIN32` block in sync.cxx.
USHORT g_cProcessorsPerGroup = 1;
USHORT g_cProcessorGroups    = 1;

enum SYNCDeadLockTimeOutState
{
    sdltosDisabled          = 0,
    sdltosEnabled           = 1,
    sdltosCheckInProgress   = 2
};
extern SYNCDeadLockTimeOutState g_sdltosState;


//  Null Synchronization Object State Initializer

const CSyncStateInitNull syncstateNull;


//  Thread Wait Notifications — function-pointer hooks for higher layers
//  (the engine uses these to record contention events).

static void OSSYNCAPI OSSyncThreadWaitBegin() {}
static void OSSYNCAPI OSSyncThreadWaitEnd()   {}

static PfnThreadWait g_pfnThreadWaitBegin = OSSyncThreadWaitBegin;
static PfnThreadWait g_pfnThreadWaitEnd   = OSSyncThreadWaitEnd;

void OSSYNCAPI OSSyncOnThreadWaitBegin( const PfnThreadWait pfn )
{
    g_pfnThreadWaitBegin = pfn ? pfn : OSSyncThreadWaitBegin;
}

void OSSYNCAPI OSSyncOnThreadWaitEnd( const PfnThreadWait pfn )
{
    g_pfnThreadWaitEnd = pfn ? pfn : OSSyncThreadWaitEnd;
}

void OnThreadWaitBegin() { g_pfnThreadWaitBegin(); }
void OnThreadWaitEnd()   { g_pfnThreadWaitEnd();   }


//  Page Memory Allocation
//
//  Win32's VirtualFree( pv, 0, MEM_RELEASE ) doesn't take a size — the
//  kernel knows the original reservation length. munmap requires a size,
//  so we keep a side-table of size-by-pointer. Allocation volume here is
//  tiny (a couple of entries during init for PLS), so a std::map under
//  a std::mutex is fine.

namespace {

// Meyers singletons — the std::map and std::mutex must be constructed
// before any caller (e.g., FOSSyncPreinit running through libese.so's
// load-time COSLayerPreInit). Plain namespace-scope globals lose the
// static-init-order race because libese.so's std.cxx COSLayerPreInit
// can fire before this TU's namespace-scope ctors run.
std::mutex& PageSizeMutex()
{
    static std::mutex m;
    return m;
}
std::map< void*, size_t >& PageSizeTable()
{
    static std::map< void*, size_t > t;
    return t;
}

void RememberPageSize( void* pv, size_t cb )
{
    std::lock_guard< std::mutex > lock( PageSizeMutex() );
    PageSizeTable()[ pv ] = cb;
}

size_t ForgetPageSize( void* pv )
{
    std::lock_guard< std::mutex > lock( PageSizeMutex() );
    auto& tab = PageSizeTable();
    auto it = tab.find( pv );
    if ( it == tab.end() )
    {
        return 0;
    }
    size_t cb = it->second;
    tab.erase( it );
    return cb;
}

} // anonymous

void* PvPageAlloc( const size_t cbSize, void* const pv )
{
    void* const pvRet = mmap( pv, cbSize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    if ( pvRet == MAP_FAILED )
    {
        return NULL;
    }
    OSSYNCAssert( !pv || pvRet == pv );
    RememberPageSize( pvRet, cbSize );
    return pvRet;
}

void* PvPageReserve( const size_t cbSize, void* const pv )
{
    void* const pvRet = mmap( pv, cbSize, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    if ( pvRet == MAP_FAILED )
    {
        return NULL;
    }
    OSSYNCAssert( !pv || pvRet == pv );
    RememberPageSize( pvRet, cbSize );
    return pvRet;
}

void PageFree( void* const pv )
{
    if ( !pv )
    {
        return;
    }
    const size_t cb = ForgetPageSize( pv );
    OSSYNCAssert( cb > 0 );
    if ( cb > 0 )
    {
        const int rc = munmap( pv, cb );
        OSSYNCAssert( rc == 0 );
        (void)rc;
    }
}

BOOL FPageCommit( void* const pv, const size_t cb )
{
    if ( !pv )
    {
        return fFalse;
    }
    return mprotect( pv, cb, PROT_READ | PROT_WRITE ) == 0 ? fTrue : fFalse;
}

void PageDecommit( void* const pv, const size_t cb )
{
    if ( !pv )
    {
        return;
    }
    //  release backing store, then mark the range non-accessible so use-after-
    //  decommit traps just like VirtualFree( MEM_DECOMMIT ).
    const int rcMadv = madvise( pv, cb, MADV_DONTNEED );
    OSSYNCAssert( rcMadv == 0 );
    (void)rcMadv;
    const int rcProt = mprotect( pv, cb, PROT_NONE );
    OSSYNCAssert( rcProt == 0 );
    (void)rcProt;
}


//  Tick Time

DWORD OSSYNCAPI DwOSSyncITickTime()
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    //  truncating to 32 bits matches GetTickCount; the engine has fixed all
    //  known 49-day rollover problems (per the upstream comment).
    return (DWORD)( (ULONG64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000 );
}


//  Context Local Storage (CLS)
//
//  In non-DEBUG builds CLS is empty (sizeof(CLS) == 1 due to C++ rules),
//  but we still allocate and zero it because Pcls() returns a stable pointer
//  and higher layers may read/write through it on debug toolchains.
//
//  We back it with a single pthread_key whose destructor frees the per-thread
//  CLS — that lets us drop the upstream global linked list of contexts.

namespace {

pthread_key_t g_clsKey;
bool          g_clsKeyInit = false;
std::mutex    g_clsKeyMutex;

void ClsDestructor( void* pv )
{
    free( pv );
}

bool EnsureClsKey()
{
    std::lock_guard< std::mutex > lock( g_clsKeyMutex );
    if ( g_clsKeyInit )
    {
        return true;
    }
    if ( pthread_key_create( &g_clsKey, ClsDestructor ) != 0 )
    {
        return false;
    }
    g_clsKeyInit = true;
    return true;
}

} // anonymous

CLS* const OSSYNCAPI Pcls()
{
    if ( !g_clsKeyInit && !EnsureClsKey() )
    {
        return NULL;
    }
    CLS* pcls = static_cast< CLS* >( pthread_getspecific( g_clsKey ) );
    if ( !pcls )
    {
        pcls = static_cast< CLS* >( calloc( 1, sizeof( CLS ) ) );
        if ( !pcls )
        {
            return NULL;
        }
        pthread_setspecific( g_clsKey, pcls );
    }
    return pcls;
}


//  Processor Information
//
//  Linux exposes a flat processor space — there's no Win32 group concept —
//  so we set g_cProcessorGroups = 1 and stuff sysconf() into per-group.

INT OSSYNCAPI OSSyncGetProcessorCountMax()
{
    return g_cProcessorsPerGroup * g_cProcessorGroups;
}

INT OSSYNCAPI OSSyncGetProcessorCount()
{
    return OSSyncGetProcessorCountMax();
}

INT OSSYNCAPI OSSyncGetCurrentProcessor()
{
    int iProc = sched_getcpu();
    if ( iProc < 0 )
    {
        iProc = 0;
    }
    const INT cProcMax = OSSyncGetProcessorCountMax();
    if ( cProcMax > 0 && iProc >= cProcMax )
    {
        iProc %= cProcMax;
    }
    return iProc;
}


//  Processor Local Storage (PLS)

namespace {
void** g_rgPLS = NULL;
} // anonymous

BOOL OSSYNCAPI FOSSyncConfigureProcessorLocalStorage( const size_t cbPLS )
{
    const size_t cbAlign    = 256;
    const size_t cbPLSAlign = ( ( cbPLS + cbAlign - 1 ) / cbAlign ) * cbAlign;

    //  release any previous allocation
    if ( g_rgPLS )
    {
        if ( g_rgPLS[ 0 ] )
        {
            PageFree( g_rgPLS[ 0 ] );
        }
        free( g_rgPLS );
        g_rgPLS = NULL;
    }

    if ( cbPLS == 0 )
    {
        return fTrue;
    }

    const size_t cProc = OSSyncGetProcessorCountMax();
    g_rgPLS = (void**)calloc( cProc, sizeof( void* ) );
    if ( !g_rgPLS )
    {
        return fFalse;
    }
    g_rgPLS[ 0 ] = PvPageAlloc( cProc * cbPLSAlign, NULL );
    if ( !g_rgPLS[ 0 ] )
    {
        free( g_rgPLS );
        g_rgPLS = NULL;
        return fFalse;
    }
    for ( size_t iPLS = 1; iPLS < cProc; iPLS++ )
    {
        g_rgPLS[ iPLS ] = (BYTE*)g_rgPLS[ 0 ] + cbPLSAlign * iPLS;
    }
    return fTrue;
}

void* OSSYNCAPI OSSyncGetProcessorLocalStorage()
{
    return OSSyncGetProcessorLocalStorage( OSSyncGetCurrentProcessor() );
}

void* OSSYNCAPI OSSyncGetProcessorLocalStorage( const size_t iProc )
{
    return ( iProc < (size_t)OSSyncGetProcessorCountMax() && g_rgPLS != NULL )
                ? g_rgPLS[ iProc ]
                : NULL;
}


//  Kernel Semaphore — POSIX sem_t on the heap, stored as void* in the
//  state's m_handle.

CKernelSemaphore::CKernelSemaphore( const CSyncBasicInfo& sbi )
    :   CEnhancedStateContainer< CKernelSemaphoreState, CSyncStateInitNull,
                                  CKernelSemaphoreInfo, CSyncBasicInfo >(
            syncstateNull, sbi )
{
}

CKernelSemaphore::~CKernelSemaphore()
{
    OSSYNCAssert( !FInitialized() );
}

const BOOL CKernelSemaphore::FInit()
{
    OSSYNCAssert( !FInitialized() );

    sem_t* psem = (sem_t*)malloc( sizeof( sem_t ) );
    if ( !psem )
    {
        return fFalse;
    }
    if ( sem_init( psem, 0, 0 ) != 0 )
    {
        free( psem );
        return fFalse;
    }
    State().SetHandle( psem );
    return fTrue;
}

void CKernelSemaphore::Term()
{
    OSSYNCAssert( FInitialized() );
    OSSYNCAssert( FReset() || g_fSyncProcessAbort );

    sem_t* psem = static_cast< sem_t* >( State().Handle() );
    sem_destroy( psem );
    free( psem );
    State().SetHandle( 0 );
}

const BOOL CKernelSemaphore::FAcquire( const INT cmsecTimeout )
{
    OSSYNCAssert( FInitialized() );
    sem_t* psem = static_cast< sem_t* >( State().Handle() );

    const bool fTimed = ( cmsecTimeout != cmsecTest );
    if ( fTimed )
    {
        State().StartWait();
    }

    BOOL fSuccess = fFalse;
    if ( cmsecTimeout == cmsecTest )
    {
        fSuccess = ( sem_trywait( psem ) == 0 ) ? fTrue : fFalse;
    }
    else if ( cmsecTimeout == cmsecInfinite ||
              cmsecTimeout == cmsecInfiniteNoDeadlock )
    {
        OnThreadWaitBegin();
        int rc;
        do { rc = sem_wait( psem ); } while ( rc == -1 && errno == EINTR );
        OnThreadWaitEnd();
        fSuccess = ( rc == 0 ) ? fTrue : fFalse;
    }
    else
    {
        const INT cmsec = cmsecTimeout < 0 ? -cmsecTimeout : cmsecTimeout;
        struct timespec ts;
        clock_gettime( CLOCK_REALTIME, &ts );
        ts.tv_sec  += cmsec / 1000;
        ts.tv_nsec += ( cmsec % 1000 ) * 1000000L;
        if ( ts.tv_nsec >= 1000000000L )
        {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1000000000L;
        }
        OnThreadWaitBegin();
        int rc;
        do { rc = sem_timedwait( psem, &ts ); } while ( rc == -1 && errno == EINTR );
        OnThreadWaitEnd();
        fSuccess = ( rc == 0 ) ? fTrue : fFalse;
    }

    if ( fTimed )
    {
        State().StopWait();
    }
    return fSuccess;
}

void CKernelSemaphore::Release( const INT cToRelease )
{
    OSSYNCAssert( FInitialized() );
    sem_t* psem = static_cast< sem_t* >( State().Handle() );
    for ( INT i = 0; i < cToRelease; i++ )
    {
        const int rc = sem_post( psem );
        OSSYNCAssert( rc == 0 );
        (void)rc;
    }
}


//  CInitTermLock convergence wait — yield the CPU once.

void CInitTermLock::SleepAwayQuanta()
{
    sched_yield();
}


//  Sync subsystem lifecycle
//
//  Mirrors the upstream FOSSyncPreinit / OSSyncPostterm contract but skips
//  the Windows-only steps: kernel-semaphore-pool DLL probing (no fallback
//  needed on Linux), SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX collection
//  (sysconf is enough), and SYNC_ENHANCED_STATE allocator (off in this build).

extern CKernelSemaphorePool g_ksempoolGlobal;

namespace {

//  Tracks whether the subsystem is initialized. Driven by AtomicIncrement /
//  AtomicDecrement so concurrent FOSSyncPreinit / OSSyncPostterm callers
//  serialize on the refcount transition.
LONG g_cOSSyncInit = 0;

} // anonymous

BOOL OSSYNCAPI FOSSyncPreinit()
{
    g_fSyncProcessAbort = fFalse;

    if ( AtomicIncrement( &g_cOSSyncInit ) != 1 )
    {
        return fTrue;
    }

    //  cache processor counts
    long cProc = sysconf( _SC_NPROCESSORS_ONLN );
    if ( cProc < 1 )
    {
        cProc = 1;
    }
    g_cProcessorGroups    = 1;
    g_cProcessorsPerGroup = (USHORT)cProc;

    //  spin count: 0 on uniprocessor, 256 otherwise (matches upstream)
    g_cSpinMax = ( cProc == 1 ) ? 0 : 256;

    //  initialize the Kernel Semaphore Pool
    if ( !g_ksempoolGlobal.FInit() )
    {
        return fFalse;
    }

    //  ensure the CLS pthread key is created up-front
    if ( !EnsureClsKey() )
    {
        return fFalse;
    }

    return fTrue;
}

void OSSYNCAPI OSSyncPostterm()
{
    if ( g_cOSSyncInit == 0 )
    {
        return;
    }
    if ( AtomicDecrement( &g_cOSSyncInit ) != 0 )
    {
        return;
    }

    //  release PLS
    FOSSyncConfigureProcessorLocalStorage( 0 );

    //  tear down the Kernel Semaphore Pool
    if ( g_ksempoolGlobal.FInitialized() )
    {
        g_ksempoolGlobal.Term();
    }
}

void OSSYNCAPI OSSyncProcessAbort()
{
    g_fSyncProcessAbort = fTrue;
}

void OSSYNCAPI OSSyncConfigDeadlockTimeoutDetection( const BOOL fEnable )
{
    g_sdltosState = fEnable ? sdltosEnabled : sdltosDisabled;
}


//  ES-memory init/term shims — SYNC_ENHANCED_STATE is disabled in this
//  build, so these reduce to the trivial path. They aren't currently in
//  libese.so's undefined symbol list but are part of the public surface;
//  keeping them keeps the static lib feature-symmetric with sync.cxx.

const BOOL OSSYNCAPI FOSSyncInitForES()
{
    return fTrue;
}

void OSSYNCAPI OSSyncTermForES()
{
}

}  // namespace OSSYNC
