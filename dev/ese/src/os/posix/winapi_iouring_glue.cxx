// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Glue between osdisk.cxx's IOREQ pool path and the io_uring submission
// layer in iouring_posix.cxx.  Lives here (not in osdisk.cxx) so the
// upstream engine source stays free of windows-shim/posix-specific
// includes — osdisk.cxx just sees a flat extern "C" entry point.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "iouring_posix.hxx"
#include "_osdisk.hxx"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // sysconf
#include <sys/uio.h>     // iovec

using osposix::ErrIOUringSubmitIOREQ;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::IOContext;
using osposix::KObject;

extern "C" DWORD OSPosixIouringSubmitIOREQ(
    HANDLE                      hFile,
    IOREQ*                      pioreq,
    bool                        fWrite,
    void*                       pvBuffer,
    DWORD                       cbData,
    QWORD                       ibOffset,
    FILE_SEGMENT_ELEMENT const* rgfse,
    DWORD                       cfse )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || k->fileFd < 0 || !pioreq )
    {
        return ERROR_INVALID_PARAMETER;
    }
    const int fd = k->fileFd;

    IOContext* const ctx = new (std::nothrow) IOContext();
    if ( !ctx )
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    ctx->op            = fWrite ? IOContext::Op::Write : IOContext::Op::Read;
    ctx->fileFd        = fd;
    ctx->ibOffset      = ibOffset;
    ctx->cbData        = cbData;
    ctx->pbData        = (BYTE*) pvBuffer;
    ctx->pioreq        = pioreq;
    ctx->pfnIOComplete = nullptr;
    ctx->iov           = nullptr;
    ctx->iovCount      = 0;

    //  Scatter/gather: convert FILE_SEGMENT_ELEMENT array (one VM page
    //  per entry, NULL-terminated) into an iovec the kernel can consume.
    //  Storage is owned by the IOContext; the completion thread frees it.
    if ( rgfse != nullptr && cfse > 0 )
    {
        const long pageSz = sysconf( _SC_PAGESIZE );
        iovec* const iov = (iovec*) calloc( cfse, sizeof( iovec ) );
        if ( !iov )
        {
            delete ctx;
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        DWORD remaining = cbData;
        int nIov = 0;
        for ( DWORD i = 0; i < cfse && rgfse[ i ].Buffer != nullptr && remaining; ++i )
        {
            const DWORD chunk = remaining < (DWORD) pageSz ? remaining : (DWORD) pageSz;
            iov[ nIov ].iov_base = rgfse[ i ].Buffer;
            iov[ nIov ].iov_len  = chunk;
            remaining -= chunk;
            ++nIov;
        }
        ctx->iov      = iov;
        ctx->iovCount = nIov;
    }

    const ERR err = ErrIOUringSubmitIOREQ( ctx );
    if ( err < JET_errSuccess )
    {
        free( ctx->iov );
        delete ctx;
        return ( err == JET_errOutOfMemory ) ? ERROR_NOT_ENOUGH_MEMORY : ERROR_IO_DEVICE;
    }
    //  Submitted — completion thread owns ctx now.  Engine sees IO as
    //  pending (success-return + pfIOCompleted=FALSE).
    return ERROR_SUCCESS;
}
