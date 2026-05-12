// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// FindFirstFileW / FindNextFileW / FindClose. Wildcards are limited to
// the engine's actual usage: an exact filename or "*"-style suffix on
// the basename. Pattern is matched with fnmatch(); fully POSIX. The
// HANDLE returned is a KObject (kind=FindFile) carrying the open DIR*
// and stored pattern for FindNext to reapply.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

using osposix::AllocKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;
using osposix::Utf8ToWide;
using osposix::WidePathToUtf8;

namespace
{
constexpr int c_pathBuf = 4096;

// FILETIME = 100-ns ticks since 1601-01-01 UTC.
constexpr uint64_t c_filetimeUnixEpochOffset = 11644473600ULL;
constexpr uint64_t c_filetimeIntervalsPerSec = 10000000ULL;

FILETIME TimespecToFileTime(const struct timespec& ts)
{
    const uint64_t total = (static_cast<uint64_t>(ts.tv_sec) + c_filetimeUnixEpochOffset)
        * c_filetimeIntervalsPerSec
        + static_cast<uint64_t>(ts.tv_nsec) / 100;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(total & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(total >> 32);
    return ft;
}

bool FillFindData(const char* dir, const char* name, LPWIN32_FIND_DATAW out)
{
    char full[c_pathBuf];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    struct stat st;
    if (lstat(full, &st) < 0)
        return false;

    memset(out, 0, sizeof(*out));
    out->dwFileAttributes = S_ISDIR(st.st_mode)
                            ? FILE_ATTRIBUTE_DIRECTORY
                            : FILE_ATTRIBUTE_NORMAL;
    if ((st.st_mode & S_IWUSR) == 0)
        out->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
    out->ftCreationTime = TimespecToFileTime(st.st_ctim);
    out->ftLastAccessTime = TimespecToFileTime(st.st_atim);
    out->ftLastWriteTime = TimespecToFileTime(st.st_mtim);
    out->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32);
    out->nFileSizeLow = static_cast<DWORD>(st.st_size & 0xFFFFFFFF);

    WCHAR wbuf[260];
    const int n = Utf8ToWide(name, wbuf, 260);
    if (n <= 0)
        return false;
    for (int i = 0; i <= n && i < 260; ++i)
        out->cFileName[i] = wbuf[i];
    out->cAlternateFileName[0] = 0;
    return true;
}

// Split path into (dir, basename) at last '/' or '\\'.
void SplitPath(const char* full, char* dir, size_t dirSz, char* base, size_t baseSz)
{
    const char* slash = nullptr;
    for (const char* p = full; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
            slash = p;
    }
    if (!slash)
    {
        strncpy(dir, ".", dirSz - 1);
        dir[dirSz - 1] = 0;
        strncpy(base, full, baseSz - 1);
        base[baseSz - 1] = 0;
        return;
    }
    const size_t cb = static_cast<size_t>(slash - full);
    const size_t cp = cb < dirSz - 1
                      ? cb
                      : dirSz - 1;
    memcpy(dir, full, cp);
    dir[cp] = 0;
    strncpy(base, slash + 1, baseSz - 1);
    base[baseSz - 1] = 0;
}
}

extern "C"
{
HANDLE FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    char dir[c_pathBuf], base[260];
    SplitPath(path, dir, sizeof(dir), base, sizeof(base));
    DIR* const d = opendir(dir);
    if (!d)
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    KObject* const k = AllocKObject(HandleKind::FindFile);
    if (!k)
    {
        closedir(d);
        return INVALID_HANDLE_VALUE;
    }
    k->findDir = d;
    k->findBaseDir = strdup(dir);
    k->findPattern = strdup(base[0]
                            ? base
                            : "*");

    // Advance to the first match.
    struct dirent* de;
    while ((de = readdir(d)) != nullptr)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (fnmatch(k->findPattern, de->d_name, 0) == 0)
        {
            if (FillFindData(k->findBaseDir, de->d_name, lpFindFileData))
            {
                return KToHandle(k);
            }
        }
    }
    // No match.
    closedir(d);
    k->findDir = nullptr;
    free(k->findBaseDir);
    k->findBaseDir = nullptr;
    free(k->findPattern);
    k->findPattern = nullptr;
    free(k);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
}

BOOL FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
{
    KObject* const k = HandleToK(hFindFile);
    if (!k || k->kind != HandleKind::FindFile)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    DIR* const d = static_cast<DIR*>(k->findDir);
    struct dirent* de;
    while ((de = readdir(d)) != nullptr)
    {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (fnmatch(k->findPattern, de->d_name, 0) == 0)
        {
            if (FillFindData(k->findBaseDir, de->d_name, lpFindFileData))
            {
                return TRUE;
            }
        }
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL FindClose(HANDLE hFindFile)
{
    if (hFindFile == INVALID_HANDLE_VALUE || !hFindFile)
        return TRUE;
    return CloseHandle(hFindFile);
}
} // extern "C"
