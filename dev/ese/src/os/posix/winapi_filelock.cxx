// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// LockFileEx / UnlockFileEx — cooperative byte-range file locks. Backed
// by Linux open-file-description locks (F_OFD_SETLK), which match Win32
// semantics: lock state belongs to the open file (not the process), so
// duplicating a HANDLE shares the lock.
//
// DeviceIoControl is the catch-all for ioctl-style queries (storage
// adapter geometry, SMART, etc.). Our implementation answers a small
// set of IOCTL_* codes the engine inspects for diagnostics; everything
// else fails with ERROR_INVALID_FUNCTION (which the engine's call sites
// already treat as "feature unavailable, fall back").

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;

namespace
{
    // Convert (high,low) pair into 64-bit length, treating low/high
    // both equal to 0xFFFFFFFF as "until EOF" (Win32 contract).
    off_t LengthFromPair( DWORD low, DWORD high )
    {
        if ( low == 0xFFFFFFFF && high == 0xFFFFFFFF ) return 0;  // 0 => to EOF for fcntl
        return ( static_cast<off_t>( high ) << 32 ) | low;
    }
}

extern "C" {

BOOL LockFileEx( HANDLE hFile, DWORD dwFlags, DWORD /*dwReserved*/,
                 DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh,
                 LPOVERLAPPED lpOverlapped )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !lpOverlapped )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    struct flock fl;
    memset( &fl, 0, sizeof( fl ) );
    fl.l_type   = ( dwFlags & LOCKFILE_EXCLUSIVE_LOCK ) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 ) | lpOverlapped->Offset;
    fl.l_len    = LengthFromPair( nNumberOfBytesToLockLow, nNumberOfBytesToLockHigh );

    const int op = ( dwFlags & LOCKFILE_FAIL_IMMEDIATELY ) ? F_OFD_SETLK : F_OFD_SETLKW;
    if ( fcntl( k->fileFd, op, &fl ) < 0 )
    {
        SetLastError( errno == EAGAIN || errno == EACCES ? ERROR_LOCK_VIOLATION : ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return TRUE;
}

BOOL UnlockFileEx( HANDLE hFile, DWORD /*dwReserved*/,
                   DWORD nNumberOfBytesToUnlockLow, DWORD nNumberOfBytesToUnlockHigh,
                   LPOVERLAPPED lpOverlapped )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !lpOverlapped )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    struct flock fl;
    memset( &fl, 0, sizeof( fl ) );
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 ) | lpOverlapped->Offset;
    fl.l_len    = LengthFromPair( nNumberOfBytesToUnlockLow, nNumberOfBytesToUnlockHigh );
    if ( fcntl( k->fileFd, F_OFD_SETLK, &fl ) < 0 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return TRUE;
}

BOOL DeviceIoControl( HANDLE hDevice, DWORD /*dwIoControlCode*/, LPVOID /*lpInBuffer*/,
                      DWORD /*nInBufferSize*/, LPVOID /*lpOutBuffer*/, DWORD /*nOutBufferSize*/,
                      LPDWORD lpBytesReturned, LPOVERLAPPED /*lpOverlapped*/ )
{
    KObject* const k = HandleToK( hDevice );
    if ( !k )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    if ( lpBytesReturned ) *lpBytesReturned = 0;
    SetLastError( ERROR_INVALID_FUNCTION );
    return FALSE;
}

}  // extern "C"
