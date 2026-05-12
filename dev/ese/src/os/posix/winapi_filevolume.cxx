// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Volume / drive information. The engine uses these mainly for crash-
// recovery diagnostics ("which volume holds the log") and free-space
// checks before extending databases. On Linux there's no per-volume
// concept matching Windows drive letters; we synthesize answers from
// statvfs() for free-space queries and return DRIVE_FIXED for any
// existing path. The Find* volume APIs that walk a system-wide volume
// list are stubbed (engine call sites tolerate FALSE).

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

using osposix::HandleKind;
using osposix::KObject;
using osposix::Utf8ToWide;
using osposix::WidePathToUtf8;

namespace
{
constexpr int c_pathBuf = 4096;
}

extern "C"
{
UINT GetDriveTypeW(LPCWSTR lpRootPathName)
{
    char path[c_pathBuf];
    if (!lpRootPathName)
        return DRIVE_FIXED;
    if (WidePathToUtf8(lpRootPathName, path, sizeof(path)) <= 0)
        return DRIVE_UNKNOWN;
    struct stat st;
    if (stat(path, &st) < 0)
        return DRIVE_NO_ROOT_DIR;
    return DRIVE_FIXED;
}

BOOL GetVolumeInformationW(LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer,
    DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
    LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
    LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
    char path[c_pathBuf];
    if (!lpRootPathName || WidePathToUtf8(lpRootPathName, path, sizeof(path)) <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    struct statvfs vfs;
    if (statvfs(path, &vfs) < 0)
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    struct stat st;
    if (stat(path, &st) < 0)
        return FALSE;

    if (lpVolumeNameBuffer && nVolumeNameSize > 0)
    {
        WCHAR wbuf[64];
        const int n = Utf8ToWide("linux", wbuf, 64);
        const DWORD cp = static_cast<DWORD>(n) < nVolumeNameSize - 1
                         ? static_cast<DWORD>(n)
                         : nVolumeNameSize - 1;
        for (DWORD i = 0; i < cp; ++i)
            lpVolumeNameBuffer[i] = wbuf[i];
        lpVolumeNameBuffer[cp] = 0;
    }
    if (lpVolumeSerialNumber)
        *lpVolumeSerialNumber = static_cast<DWORD>(st.st_dev);
    if (lpMaximumComponentLength)
        *lpMaximumComponentLength = static_cast<DWORD>(vfs.f_namemax);
    if (lpFileSystemFlags)
        *lpFileSystemFlags = 0;
    if (lpFileSystemNameBuffer && nFileSystemNameSize > 0)
    {
        WCHAR wbuf[16];
        const int n = Utf8ToWide("ext4", wbuf, 16);
        const DWORD cp = static_cast<DWORD>(n) < nFileSystemNameSize - 1
                         ? static_cast<DWORD>(n)
                         : nFileSystemNameSize - 1;
        for (DWORD i = 0; i < cp; ++i)
            lpFileSystemNameBuffer[i] = wbuf[i];
        lpFileSystemNameBuffer[cp] = 0;
    }
    return TRUE;
}

BOOL GetVolumePathNameW(LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength)
{
    // Linux has no per-file volume root; return the first path component
    // ('/' for absolute paths, the engine treats this as "the volume").
    if (!lpszVolumePathName || cchBufferLength < 2)
        return FALSE;
    (void) lpszFileName;
    lpszVolumePathName[0] = L'/';
    lpszVolumePathName[1] = 0;
    return TRUE;
}

BOOL GetVolumeNameForVolumeMountPointW(LPCWSTR /*lpszVolumeMountPoint*/, LPWSTR lpszVolumeName,
    DWORD cchBufferLength)
{
    if (!lpszVolumeName || cchBufferLength == 0)
        return FALSE;
    // Synthesize a stable identifier; engine uses it for diagnostic
    // logging only.
    const WCHAR* const literal = L"\\\\?\\Volume{linux-root}\\";
    DWORD i = 0;
    while (literal[i] && i + 1 < cchBufferLength)
    {
        lpszVolumeName[i] = literal[i];
        ++i;
    }
    lpszVolumeName[i] = 0;
    return TRUE;
}

BOOL GetDiskFreeSpaceW(LPCWSTR lpRootPathName, LPDWORD lpSectorsPerCluster,
    LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
    LPDWORD lpTotalNumberOfClusters)
{
    char path[c_pathBuf];
    if (!lpRootPathName || WidePathToUtf8(lpRootPathName, path, sizeof(path)) <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    struct statvfs vfs;
    if (statvfs(path, &vfs) < 0)
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    if (lpSectorsPerCluster)
        *lpSectorsPerCluster = 1;
    if (lpBytesPerSector)
        *lpBytesPerSector = static_cast<DWORD>(vfs.f_bsize);
    if (lpNumberOfFreeClusters)
        *lpNumberOfFreeClusters = static_cast<DWORD>(vfs.f_bavail);
    if (lpTotalNumberOfClusters)
        *lpTotalNumberOfClusters = static_cast<DWORD>(vfs.f_blocks);
    return TRUE;
}

BOOL GetDiskFreeSpaceExW(LPCWSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailable,
    PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes)
{
    char path[c_pathBuf];
    if (!lpDirectoryName || WidePathToUtf8(lpDirectoryName, path, sizeof(path)) <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    struct statvfs vfs;
    if (statvfs(path, &vfs) < 0)
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    const uint64_t bs = vfs.f_bsize;
    if (lpFreeBytesAvailable)
        lpFreeBytesAvailable->QuadPart = bs * vfs.f_bavail;
    if (lpTotalNumberOfBytes)
        lpTotalNumberOfBytes->QuadPart = bs * vfs.f_blocks;
    if (lpTotalNumberOfFreeBytes)
        lpTotalNumberOfFreeBytes->QuadPart = bs * vfs.f_bfree;
    return TRUE;
}

// Volume-enumeration stubs. Engine uses these to walk volumes for
// crash-recovery file-id resolution. Linux equivalent (parsing
// /proc/mounts) is significant; left as TBD because the engine's
// fallback path (path-based open) handles the absence cleanly.
HANDLE FindFirstVolumeW(LPWSTR /*lpszVolumeName*/, DWORD /*cchBufferLength*/)
{
    SetLastError(ERROR_NO_MORE_FILES);
    return INVALID_HANDLE_VALUE;
}

BOOL FindNextVolumeW(HANDLE /*hFindVolume*/, LPWSTR /*lpszVolumeName*/, DWORD /*cchBufferLength*/)
{
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL FindVolumeClose(HANDLE hFindVolume)
{
    if (hFindVolume == INVALID_HANDLE_VALUE || !hFindVolume)
        return TRUE;
    return CloseHandle(hFindVolume);
}

BOOL GetVolumePathNamesForVolumeNameW(LPCWSTR /*lpszVolumeName*/, LPWSTR lpszVolumePathNames,
    DWORD cchBufferLength, PDWORD lpcchReturnLength)
{
    // Synthesize "/\0\0" — engine reads a double-null-terminated list.
    if (cchBufferLength < 3 || !lpszVolumePathNames)
    {
        if (lpcchReturnLength)
            *lpcchReturnLength = 3;
        SetLastError(ERROR_MORE_DATA);
        return FALSE;
    }
    lpszVolumePathNames[0] = L'/';
    lpszVolumePathNames[1] = 0;
    lpszVolumePathNames[2] = 0;
    if (lpcchReturnLength)
        *lpcchReturnLength = 3;
    return TRUE;
}

DWORD QueryDosDeviceW(LPCWSTR /*lpDeviceName*/, LPWSTR /*lpTargetPath*/, DWORD /*ucchMax*/)
{
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 0;
}
} // extern "C"
