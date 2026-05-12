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
#include <sys/stat.h>
#include <unistd.h>

using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;

namespace
{
// Convert (high,low) pair into 64-bit length, treating low/high
// both equal to 0xFFFFFFFF as "until EOF" (Win32 contract).
off_t LengthFromPair(DWORD low, DWORD high)
{
    if (low == 0xFFFFFFFF && high == 0xFFFFFFFF)
        return 0; // 0 => to EOF for fcntl
    return (static_cast<off_t>(high) << 32) | low;
}
}

extern "C"
{
BOOL LockFileEx(HANDLE hFile, DWORD dwFlags, DWORD /*dwReserved*/,
    DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh,
    LPOVERLAPPED lpOverlapped)
{
    KObject* const k = HandleToK(hFile);
    if (!k || k->kind != HandleKind::File || !lpOverlapped)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = (dwFlags & LOCKFILE_EXCLUSIVE_LOCK)
                ? F_WRLCK
                : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset;
    fl.l_len = LengthFromPair(nNumberOfBytesToLockLow, nNumberOfBytesToLockHigh);

    const int op = (dwFlags & LOCKFILE_FAIL_IMMEDIATELY)
                   ? F_OFD_SETLK
                   : F_OFD_SETLKW;
    int rc;
    while ((rc = fcntl(k->fileFd, op, &fl)) < 0 && errno == EINTR)
    {
        //  F_OFD_SETLKW can be interrupted by a signal; the caller asked
        //  for a blocking acquire, so retry rather than surface EINTR.
    }
    if (rc < 0)
    {
        //  EAGAIN/EACCES from F_OFD_SETLK means the lock is held — this
        //  IS the right Win32 error for a non-blocking lock attempt that
        //  contended.  Other errors are programming errors.
        SetLastError((errno == EAGAIN || errno == EACCES)
                     ? ERROR_LOCK_VIOLATION
                     : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL UnlockFileEx(HANDLE hFile, DWORD /*dwReserved*/,
    DWORD nNumberOfBytesToUnlockLow, DWORD nNumberOfBytesToUnlockHigh,
    LPOVERLAPPED lpOverlapped)
{
    KObject* const k = HandleToK(hFile);
    if (!k || k->kind != HandleKind::File || !lpOverlapped)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset;
    fl.l_len = LengthFromPair(nNumberOfBytesToUnlockLow, nNumberOfBytesToUnlockHigh);
    if (fcntl(k->fileFd, F_OFD_SETLK, &fl) < 0)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

namespace
{
//  FSCTL_SET_ZERO_DATA — punch a hole in a sparse file.  fallocate with
//  PUNCH_HOLE|KEEP_SIZE matches the Win32 contract (logical size unchanged).
BOOL IoctlPunchHole(KObject* const k, LPVOID lpInBuffer, DWORD nInBufferSize)
{
    if (nInBufferSize < sizeof(FILE_ZERO_DATA_INFORMATION) || !lpInBuffer)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const FILE_ZERO_DATA_INFORMATION* const pzdi = (const FILE_ZERO_DATA_INFORMATION*) lpInBuffer;
    const off_t ibStart = (off_t) pzdi->FileOffset.QuadPart;
    const off_t ibEnd = (off_t) pzdi->BeyondFinalZero.QuadPart;
    if (ibEnd <= ibStart)
    {
        return TRUE;
    }
    if (fallocate(k->fileFd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, ibStart, ibEnd - ibStart) < 0)
    {
        SetLastError(errno == EOPNOTSUPP
                     ? ERROR_INVALID_FUNCTION
                     : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

//  FSCTL_QUERY_ALLOCATED_RANGES — Win32 returns an array of allocated
//  byte ranges that intersect the input range.  Linux equivalent uses
//  lseek with SEEK_DATA / SEEK_HOLE to walk the allocated extents.
BOOL IoctlQueryAllocatedRanges(
    KObject* const k,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned)
{
    if (nInBufferSize < sizeof(FILE_ALLOCATED_RANGE_BUFFER) || !lpInBuffer || !lpOutBuffer || !lpBytesReturned)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const auto pIn = (const FILE_ALLOCATED_RANGE_BUFFER*) lpInBuffer;
    const auto pOut = (FILE_ALLOCATED_RANGE_BUFFER*) lpOutBuffer;
    const auto ibQueryStart = (off_t) pIn->FileOffset.QuadPart;
    const off_t ibQueryEnd = ibQueryStart + (off_t) pIn->Length.QuadPart;
    const size_t cMaxOut = nOutBufferSize / sizeof(FILE_ALLOCATED_RANGE_BUFFER);

    *lpBytesReturned = 0;
    size_t iOut = 0;
    off_t ibCursor = ibQueryStart;
    while (ibCursor < ibQueryEnd && iOut < cMaxOut)
    {
        const off_t ibData = lseek(k->fileFd, ibCursor, SEEK_DATA);
        if (ibData < 0)
        {
            if (errno == ENXIO)
            {
                //  No more data — remainder of file is a hole.
                break;
            }
            SetLastError(ERROR_INVALID_FUNCTION);
            return FALSE;
        }
        if (ibData >= ibQueryEnd)
        {
            break;
        }
        off_t ibHole = lseek(k->fileFd, ibData, SEEK_HOLE);
        if (ibHole < 0)
        {
            //  SEEK_HOLE always succeeds — EOF acts as an implicit hole.
            SetLastError(ERROR_INVALID_FUNCTION);
            return FALSE;
        }
        const off_t ibSegEnd = ibHole > ibQueryEnd
                               ? ibQueryEnd
                               : ibHole;
        pOut[iOut].FileOffset.QuadPart = ibData;
        pOut[iOut].Length.QuadPart = ibSegEnd - ibData;
        ++iOut;
        ibCursor = ibHole;
    }
    *lpBytesReturned = (DWORD) (iOut * sizeof(FILE_ALLOCATED_RANGE_BUFFER));
    return TRUE;
}
} //  namespace

//  Storage-property / disk-cache / volume-disk-extents IOCTLs live in
//  winapi_blockdev_ioctl.cxx so the sysfs-walking lives next to its
//  helpers.
BOOL OSPosixHandleStorageIoctl(HANDLE hDevice, DWORD dwIoControlCode,
    LPVOID lpInBuffer, DWORD nInBufferSize,
    LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned);

BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer,
    DWORD nInBufferSize, LPVOID lpOutBuffer, DWORD nOutBufferSize,
    LPDWORD lpBytesReturned, LPOVERLAPPED /*lpOverlapped*/)
{
    KObject* const k = HandleToK(hDevice);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (lpBytesReturned)
    {
        *lpBytesReturned = 0;
    }

    switch (dwIoControlCode)
    {
        case FSCTL_MARK_HANDLE:
        case FSCTL_SET_SPARSE:
            //  No-op success on Linux: read-copy steering has no equivalent,
            //  and every filesystem ESE runs on is sparse-by-default.
            return TRUE;

        case FSCTL_SET_ZERO_DATA:
            return IoctlPunchHole(k, lpInBuffer, nInBufferSize);

        case FSCTL_QUERY_ALLOCATED_RANGES:
            return IoctlQueryAllocatedRanges(k, lpInBuffer, nInBufferSize,
                lpOutBuffer, nOutBufferSize, lpBytesReturned);

        case IOCTL_STORAGE_QUERY_PROPERTY:
        case IOCTL_DISK_GET_CACHE_INFORMATION:
        case IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS:
            return OSPosixHandleStorageIoctl(hDevice, dwIoControlCode,
                lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesReturned);
    }

    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}
} // extern "C"
