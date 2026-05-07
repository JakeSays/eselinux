// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal header for the process-global io_uring + completion thread.
// Owns the lifecycle (ErrIOUringInit / IOUringTerm) and the IOContext
// shape that CIoUringFile fills out for each in-flight read/write.
#pragma once

#include "osstd.hxx"

#include <pthread.h>

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

    //  Engine-supplied callback context. The PfnIOComplete callback (if
    //  non-null) is invoked by the completion thread after the io_uring
    //  CQE arrives.
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

    //  Sync-wait support — only initialized when pfnIOComplete == null.
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

void DestroyContextSyncWait( IOContext* ctx );

}  // namespace osposix
