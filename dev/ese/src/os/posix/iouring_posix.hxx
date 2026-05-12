// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal header for the process-global io_uring + completion thread.
// Owns the lifecycle (ErrIOUringInit / IOUringTerm) and the IOContext
// shape that CIoUringFile fills out for each in-flight read/write.
#pragma once

#include "osstd.hxx"

#include <pthread.h>

class IOREQ;
struct iovec;

namespace osposix
{

struct IOContext
{
    enum class Op
    {
        Read,
        Write,
        Fsync,
    };

    //  Completion routing — exactly one of these is set:
    //    pioreq != null            -> osdisk.cxx IOREQ pool path; CQE
    //                                 dispatches to OSDiskIIOThreadCompleteWithErr.
    //    pfnIOComplete != null     -> generic async IFileAPI callback.
    //    both null                 -> sync wait via the cond below.
    Op                  op;
    int                 fileFd;
    IFileAPI*           fapi;
    TraceContext        tc;
    OSFILEQOS           qos;
    QWORD               ibOffset;
    DWORD               cbData;
    BYTE*               pbData;
    DWORD_PTR           keyIOComplete;
    IFileAPI::PfnIOComplete pfnIOComplete;
    IOREQ*              pioreq;

    //  Vector buffer — only set for scatter/gather submissions.  Submitter
    //  owns the storage; completion thread frees it when freeing IOContext.
    iovec*              iov;
    int                 iovCount;

    //  Sync-wait support — only initialized when pfnIOComplete == null
    //  AND pioreq == null.
    //  The completion thread signals cond; the submitter pthread_cond_waits.
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
    bool                done;
    int                 res;        //  JET_err set by completion thread
};

ERR  ErrIOUringInit();
void IOUringTerm();

ERR  ErrIOUringRead(  int fd, IOContext* ctx );
ERR  ErrIOUringWrite( int fd, IOContext* ctx );
ERR  ErrIOUringFsync( int fd );

//  IOREQ-pool async submission entry.  ctx->pioreq must be set; ctx is
//  heap-allocated by the caller and freed by the completion thread.
//  For vector (scatter/gather) submissions, set ctx->iov + ctx->iovCount;
//  pbData/cbData are ignored.
ERR  ErrIOUringSubmitIOREQ( IOContext* ctx );

void DestroyContextSyncWait( IOContext* ctx );

}  // namespace osposix
