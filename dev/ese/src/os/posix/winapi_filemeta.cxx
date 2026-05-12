// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// File-system metadata operations: directory create/remove, attribute
// query/set, delete/move/copy, path resolution, GetFinalPathNameByHandleW
// (via /proc/self/fd/N readlink), and the FILE_INFO_BY_HANDLE_CLASS
// dispatcher used by GetFileInformationByHandleEx /
// SetFileInformationByHandle. OpenFileById is a stub that fails — engine
// callers fall back to path-based open on failure.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <unistd.h>

using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::Utf8ToWide;
using osposix::WidePathToUtf8;

namespace
{
constexpr int c_pathBuf = 4096;

DWORD AttributesFromMode(mode_t m)
{
    DWORD attrs = 0;
    if (S_ISDIR(m))
        attrs |= FILE_ATTRIBUTE_DIRECTORY;
    if ((m & S_IWUSR) == 0)
        attrs |= FILE_ATTRIBUTE_READONLY;
    if (attrs == 0)
        attrs = FILE_ATTRIBUTE_NORMAL;
    return attrs;
}
}

extern "C"
{
BOOL DeleteFileW(LPCWSTR lpFileName)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
        return FALSE;
    if (unlink(path) < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

BOOL MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName)
{
    char from[c_pathBuf], to[c_pathBuf];
    if (WidePathToUtf8(lpExistingFileName, from, sizeof(from)) <= 0)
        return FALSE;
    if (WidePathToUtf8(lpNewFileName, to, sizeof(to)) <= 0)
        return FALSE;
    if (rename(from, to) < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : errno == EEXIST
                       ? ERROR_ALREADY_EXISTS
                       : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

BOOL MoveFileExW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags)
{
    char from[c_pathBuf], to[c_pathBuf];
    if (WidePathToUtf8(lpExistingFileName, from, sizeof(from)) <= 0)
        return FALSE;
    if (WidePathToUtf8(lpNewFileName, to, sizeof(to)) <= 0)
        return FALSE;
    if (!(dwFlags & MOVEFILE_REPLACE_EXISTING))
    {
        struct stat st;
        if (stat(to, &st) == 0)
        {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
    }
    if (rename(from, to) < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists)
{
    char from[c_pathBuf], to[c_pathBuf];
    if (WidePathToUtf8(lpExistingFileName, from, sizeof(from)) <= 0)
        return FALSE;
    if (WidePathToUtf8(lpNewFileName, to, sizeof(to)) <= 0)
        return FALSE;

    const int srcFd = open(from, O_RDONLY | O_CLOEXEC);
    if (srcFd < 0)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    const int dstFlags = O_WRONLY | O_CREAT | (bFailIfExists
                                               ? O_EXCL
                                               : O_TRUNC) | O_CLOEXEC;
    const int dstFd = open(to, dstFlags, 0644);
    if (dstFd < 0)
    {
        close(srcFd);
        SetLastError(errno == EEXIST
                     ? ERROR_FILE_EXISTS
                     : ERROR_ACCESS_DENIED);
        return FALSE;
    }

    struct stat st;
    if (fstat(srcFd, &st) < 0)
    {
        close(srcFd);
        close(dstFd);
        return FALSE;
    }
    off_t off = 0;
    while (off < st.st_size)
    {
        const ssize_t n = sendfile(dstFd, srcFd, &off, st.st_size - off);
        if (n <= 0)
            break;
    }
    close(srcFd);
    close(dstFd);
    return TRUE;
}

DWORD GetFileAttributesW(LPCWSTR lpFileName)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
        return INVALID_FILE_ATTRIBUTES;
    struct stat st;
    if (stat(path, &st) < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : ERROR_ACCESS_DENIED);
        return INVALID_FILE_ATTRIBUTES;
    }
    return AttributesFromMode(st.st_mode);
}

//  Returns the on-disk allocated size of the file (compressed/sparse aware).
//  Linux equivalent: st_blocks * 512 (POSIX defines st_blocks as 512-byte
//  units regardless of filesystem block size).  Returns INVALID_FILE_SIZE
//  with GetLastError() set on failure, matching Win32 semantics.
DWORD GetCompressedFileSizeW(LPCWSTR lpFileName, LPDWORD lpFileSizeHigh)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
    {
        if (lpFileSizeHigh)
        {
            *lpFileSizeHigh = 0;
        }
        return INVALID_FILE_SIZE;
    }
    struct stat st;
    if (stat(path, &st) < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : ERROR_ACCESS_DENIED);
        if (lpFileSizeHigh)
        {
            *lpFileSizeHigh = 0;
        }
        return INVALID_FILE_SIZE;
    }
    const uint64_t cbAllocated = (uint64_t) st.st_blocks * 512;
    if (lpFileSizeHigh)
    {
        *lpFileSizeHigh = (DWORD) (cbAllocated >> 32);
    }
    SetLastError(ERROR_SUCCESS);
    return (DWORD) (cbAllocated & 0xFFFFFFFFu);
}

//  GetFileAttributesExW — extended stat-by-path (attributes + timestamps + size).
//  Engine uses GetFileExInfoStandard to populate WIN32_FILE_ATTRIBUTE_DATA.
BOOL GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId,
                          LPVOID lpFileInformation)
{
    if (fInfoLevelId != GetFileExInfoStandard || !lpFileInformation)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
        return FALSE;
    struct stat st;
    if (stat(path, &st) < 0)
    {
        SetLastError(errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    WIN32_FILE_ATTRIBUTE_DATA* const pd = (WIN32_FILE_ATTRIBUTE_DATA*) lpFileInformation;
    pd->dwFileAttributes = AttributesFromMode(st.st_mode);
    //  FILETIME = 100-ns ticks since 1601-01-01 UTC.  Linux's st_mtime is
    //  POSIX epoch (1970); shift by 11644473600 seconds.
    const uint64_t c_epochShift = 11644473600ull;
    auto FillFt = [c_epochShift](FILETIME& ft, time_t sec, long nsec)
    {
        const uint64_t v = ((uint64_t) (sec + (time_t) c_epochShift)) * 10000000ull + (uint64_t) (nsec / 100);
        ft.dwLowDateTime = (DWORD) (v & 0xFFFFFFFFu);
        ft.dwHighDateTime = (DWORD) (v >> 32);
    };
    FillFt(pd->ftCreationTime,   st.st_ctime, 0);
    FillFt(pd->ftLastAccessTime, st.st_atime, 0);
    FillFt(pd->ftLastWriteTime,  st.st_mtime, 0);
    pd->nFileSizeLow  = (DWORD) ((uint64_t) st.st_size & 0xFFFFFFFFu);
    pd->nFileSizeHigh = (DWORD) ((uint64_t) st.st_size >> 32);
    return TRUE;
}

BOOL SetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
        return FALSE;
    struct stat st;
    if (stat(path, &st) < 0)
        return FALSE;
    mode_t m = st.st_mode;
    if (dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        m &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    else
        m |= S_IWUSR;
    return (chmod(path, m) == 0)
           ? TRUE
           : FALSE;
}

BOOL CreateDirectoryW(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES /*lpSecurityAttributes*/)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpPathName, path, sizeof(path)) <= 0)
        return FALSE;
    if (mkdir(path, 0755) < 0)
    {
        SetLastError(errno == EEXIST
                     ? ERROR_ALREADY_EXISTS
                     : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return TRUE;
}

BOOL RemoveDirectoryW(LPCWSTR lpPathName)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpPathName, path, sizeof(path)) <= 0)
        return FALSE;
    return (rmdir(path) == 0)
           ? TRUE
           : FALSE;
}

DWORD GetFullPathNameW(LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
        return 0;

    // realpath fails for nonexistent files; engine call sites pass paths
    // that may not exist yet (eg pre-create). Use a manual absolutize:
    // if path is relative, prefix CWD.
    char abs[c_pathBuf];
    if (path[0] == '/')
    {
        strncpy(abs, path, sizeof(abs) - 1);
        abs[sizeof(abs) - 1] = 0;
    }
    else
    {
        char cwd[c_pathBuf];
        if (!getcwd(cwd, sizeof(cwd)))
            return 0;
        snprintf(abs, sizeof(abs), "%s/%s", cwd, path);
    }

    WCHAR wbuf[c_pathBuf];
    const int n = Utf8ToWide(abs, wbuf, c_pathBuf);
    if (n <= 0)
        return 0;

    if (nBufferLength == 0 || lpBuffer == nullptr)
    {
        return static_cast<DWORD>(n + 1);
    }
    if (static_cast<DWORD>(n) >= nBufferLength)
    {
        return static_cast<DWORD>(n + 1); // buffer too small
    }
    for (int i = 0; i <= n; ++i)
        lpBuffer[i] = wbuf[i];

    if (lpFilePart)
    {
        // Point at the basename (after last '/' / '\\').
        LPWSTR last = nullptr;
        for (int i = 0; i < n; ++i)
        {
            if (lpBuffer[i] == L'/' || lpBuffer[i] == L'\\')
            {
                last = lpBuffer + i + 1;
            }
        }
        *lpFilePart = last;
    }
    return static_cast<DWORD>(n);
}

DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    char cwd[c_pathBuf];
    if (!getcwd(cwd, sizeof(cwd)))
        return 0;
    WCHAR wbuf[c_pathBuf];
    const int n = Utf8ToWide(cwd, wbuf, c_pathBuf);
    if (n <= 0)
        return 0;
    if (nBufferLength == 0 || lpBuffer == nullptr || static_cast<DWORD>(n) >= nBufferLength)
    {
        return static_cast<DWORD>(n + 1);
    }
    for (int i = 0; i <= n; ++i)
        lpBuffer[i] = wbuf[i];
    return static_cast<DWORD>(n);
}

DWORD GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer)
{
    const char* tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    char buf[c_pathBuf];
    snprintf(buf, sizeof(buf), "%s/", tmp);
    WCHAR wbuf[c_pathBuf];
    const int n = Utf8ToWide(buf, wbuf, c_pathBuf);
    if (n <= 0)
        return 0;
    if (nBufferLength == 0 || lpBuffer == nullptr || static_cast<DWORD>(n) >= nBufferLength)
    {
        return static_cast<DWORD>(n + 1);
    }
    for (int i = 0; i <= n; ++i)
        lpBuffer[i] = wbuf[i];
    return static_cast<DWORD>(n);
}

UINT GetTempFileNameW(LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName)
{
    char dir[c_pathBuf], prefix[32];
    if (WidePathToUtf8(lpPathName, dir, sizeof(dir)) <= 0)
        return 0;
    if (WidePathToUtf8(lpPrefixString, prefix, sizeof(prefix)) <= 0)
    {
        prefix[0] = 't';
        prefix[1] = 0;
    }
    char path[c_pathBuf];
    UINT unique = uUnique != 0
                  ? uUnique
                  : static_cast<UINT>(getpid());
    snprintf(path, sizeof(path), "%s/%s%04X.TMP", dir, prefix, unique & 0xFFFF);
    if (uUnique == 0)
    {
        // Win32 contract: when uUnique==0, function generates unique name
        // and creates the file. Use mkstemp-style retry with O_EXCL.
        for (int tries = 0; tries < 1024; ++tries)
        {
            const int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
            if (fd >= 0)
            {
                close(fd);
                break;
            }
            if (errno != EEXIST)
                return 0;
            ++unique;
            snprintf(path, sizeof(path), "%s/%s%04X.TMP", dir, prefix, unique & 0xFFFF);
        }
    }
    WCHAR wbuf[c_pathBuf];
    const int n = Utf8ToWide(path, wbuf, c_pathBuf);
    if (n <= 0)
        return 0;
    for (int i = 0; i <= n; ++i)
        lpTempFileName[i] = wbuf[i];
    return unique;
}

DWORD GetFinalPathNameByHandleW(HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD /*dwFlags*/)
{
    KObject* const k = HandleToK(hFile);
    if (!k || k->kind != HandleKind::File)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    char proc[64];
    snprintf(proc, sizeof(proc), "/proc/self/fd/%d", k->fileFd);
    char target[c_pathBuf];
    const ssize_t n = readlink(proc, target, sizeof(target) - 1);
    if (n <= 0)
        return 0;
    target[n] = 0;
    WCHAR wbuf[c_pathBuf];
    const int wn = Utf8ToWide(target, wbuf, c_pathBuf);
    if (wn <= 0)
        return 0;
    if (cchFilePath == 0 || lpszFilePath == nullptr || static_cast<DWORD>(wn) >= cchFilePath)
    {
        return static_cast<DWORD>(wn + 1);
    }
    for (int i = 0; i <= wn; ++i)
        lpszFilePath[i] = wbuf[i];
    return static_cast<DWORD>(wn);
}

// OpenFileById — Linux file IDs aren't directly openable; engine call
// sites use this only as a fast path (path-based open is the fallback).
HANDLE OpenFileById(HANDLE /*hVolumeHint*/, LPFILE_ID_DESCRIPTOR /*lpFileId*/, DWORD /*dwDesiredAccess*/,
    DWORD /*dwShareMode*/, LPSECURITY_ATTRIBUTES /*lpSecurityAttributes*/,
    DWORD /*dwFlagsAndAttributes*/)
{
    SetLastError(ERROR_INVALID_FUNCTION);
    return INVALID_HANDLE_VALUE;
}

// FILE_INFO_BY_HANDLE_CLASS handlers — engine call sites that fall in
// here will surface as undefined references in the next layer, at which
// point we wire up the specific class. For now, all classes return
// ERROR_INVALID_PARAMETER so any caller knows to pick a fallback.
BOOL GetFileInformationByHandleEx(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS /*FileInformationClass*/,
    LPVOID /*lpFileInformation*/, DWORD /*dwBufferSize*/)
{
    KObject* const k = HandleToK(hFile);
    if (!k || k->kind != HandleKind::File)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

BOOL SetFileInformationByHandle(HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS /*FileInformationClass*/,
    LPVOID /*lpFileInformation*/, DWORD /*dwBufferSize*/)
{
    KObject* const k = HandleToK(hFile);
    if (!k || k->kind != HandleKind::File)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
} // extern "C"

//  GetTempPath2W (Win10+) and NtQueryVolumeInformationFile are forward-
//  declared in upstream osfs.cxx without an extern "C" wrapper, so they
//  have C++ linkage from the engine's perspective.  Provide matching
//  C++-linkage definitions here; bodies just trampoline to the
//  POSIX-equivalent code paths.
DWORD GetTempPath2W(DWORD nBufferLength, LPWSTR lpBuffer)
{
    return GetTempPathW(nBufferLength, lpBuffer);
}

