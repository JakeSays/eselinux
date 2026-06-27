// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// CreateFileW / ReadFile / WriteFile / scatter-gather / size / seek /
// truncate / flush / GetFileInformationByHandle. Backs a Win32 file
// HANDLE with a FileObject (fileFd holds the OS fd).
//
// OVERLAPPED handling here is synchronous — pread/pwrite use the offset
// in OVERLAPPED but do not signal any event or schedule async completion.
// The engine wraps real async work through the io_uring task layer
// (winapi_task.cxx, future); this file is the simple inline path.
//
// FILE_FLAG_NO_BUFFERING maps to O_DIRECT (kernel honors alignment
// requirements). FILE_FLAG_WRITE_THROUGH maps to O_SYNC. FILE_FLAG_
// DELETE_ON_CLOSE is recorded in fileDeleteOnClosePath and the unlink
// happens when the FileObject is destroyed.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>   // flock
#include <sys/sysmacros.h>  // major/minor for PHYSICALDRIVE decode
#include <sys/uio.h>
#include <unistd.h>

using osposix::As;
using osposix::FileObject;
using osposix::KToHandle;
using osposix::Utf8ToWide;
using osposix::WidePathToUtf8;

namespace
{
constexpr int c_pathBuf = 4096;
// FILETIME = 100-ns ticks since 1601-01-01 UTC; UNIX epoch offset.
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

int OpenFlagsFromWin32(DWORD desiredAccess, DWORD shareMode, DWORD creation, DWORD flags)
{
    int oflags = 0;
    const bool wantRead = (desiredAccess & GENERIC_READ) != 0;
    const bool wantWrite = (desiredAccess & GENERIC_WRITE) != 0;
    if (wantRead && wantWrite)
        oflags = O_RDWR;
    else if (wantWrite)
        oflags = O_WRONLY;
    else
        oflags = O_RDONLY;

    switch (creation)
    {
        case CREATE_NEW:
            oflags |= O_CREAT | O_EXCL;
            break;
        case CREATE_ALWAYS:
            oflags |= O_CREAT | O_TRUNC;
            break;
        case OPEN_EXISTING:
            break;
        case OPEN_ALWAYS:
            oflags |= O_CREAT;
            break;
        case TRUNCATE_EXISTING:
            oflags |= O_TRUNC;
            break;
    }

    if (flags & FILE_FLAG_NO_BUFFERING)
        oflags |= O_DIRECT;
    if (flags & FILE_FLAG_WRITE_THROUGH)
        oflags |= O_SYNC;
    oflags |= O_CLOEXEC;
    (void) shareMode;
    return oflags;
}

// Translate Win32 dwShareMode into a flock(2) advisory lock mode.
// CreateFileW's shareMode says what *other* opens may do while this
// handle is alive; POSIX has no primary-handle concept, so we
// approximate with whole-file advisory locks:
//
//   shareMode == 0                  -> LOCK_EX (deny everyone)
//   FILE_SHARE_READ only            -> LOCK_EX (deny writers; readers
//                                      get denied too — closest we
//                                      can do without mandatory locks)
//   FILE_SHARE_WRITE present        -> 0 (don't lock — caller is
//                                      tolerating another writer)
//
// Return 0 for "no lock", LOCK_EX otherwise. Used non-blocking so
// CreateFileW maps a busy share to ERROR_SHARING_VIOLATION.
int FlockModeFromShareMode(DWORD shareMode)
{
    if ((shareMode & FILE_SHARE_WRITE) != 0)
    {
        return 0;
    }
    return LOCK_EX;
}

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

//  Length-counted prefixes for Win32 device path detection.  Templated so
//  sizeof(literal) gives us the compile-time length and we don't sprinkle
//  raw character counts through the parser.  Templates can't have C linkage,
//  so these live in the anonymous namespace above the extern "C" block.
template <size_t N>
constexpr size_t PrefixLen(const char (&)[N])
{
    return N - 1;  //  drop trailing NUL
}

template <size_t N>
bool HasPrefixI(const char* path, const char (&prefix)[N])
{
    return strncasecmp(path, prefix, N - 1) == 0;
}

template <size_t N>
bool HasPrefix(const char* path, const char (&prefix)[N])
{
    return strncmp(path, prefix, N - 1) == 0;
}

//  Detect engine-synthesized "block device" path syntax and route to the
//  BlockDevice KObject builder so DeviceIoControl can answer storage queries
//  via /sys/block.  No real fd is opened.
//
//   \\?\Volume{MAJ-MIN}\        -> filesystem with that (major:minor)
//   \\?\Volume{MAJ-MIN}         -> same, no trailing slash (engine strips it)
//   \\.\PHYSICALDRIVE{N}        -> whole-disk handle by index — synthesize
//                                  with minor=N (engine uses this as an
//                                  opaque identifier; we read sysfs by the
//                                  associated name when we can find one)
HANDLE OpenSyntheticBlockDevice(const char* path)
{
    unsigned int major = 0, minor = 0;
    char diskName[64] = "";

    //  Accept both Win32 (\\?\) and engine-normalized (//?/) forms.
    static constexpr char c_volumePrefixWin[]   = "\\\\?\\Volume{";
    static constexpr char c_volumePrefixUnix[]  = "//?/Volume{";
    const char* volumeSuffix = nullptr;
    if (HasPrefix(path, c_volumePrefixWin))
        volumeSuffix = path + PrefixLen(c_volumePrefixWin);
    else if (HasPrefix(path, c_volumePrefixUnix))
        volumeSuffix = path + PrefixLen(c_volumePrefixUnix);

    if (volumeSuffix)
    {
        if (sscanf(volumeSuffix, "%u-%u", &major, &minor) != 2)
        {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        //  Walk /sys/dev/block/X:Y -> kernel device name + whole-disk parent
        //  (major:minor).  Best-effort: filesystems with no /sys/dev/block
        //  entry (ZFS pools, tmpfs, NFS, overlay, fuse, anything backed by
        //  an "anonymous" major=0 super_block) still produce a working
        //  HANDLE — the (major:minor) is unique per filesystem instance,
        //  which is what the engine's COSDisk grouping actually needs.
        //  IOCTLs that require real hardware info fall through to defaults
        //  inside the IOCTL dispatcher when blockDiskName is empty.
        unsigned int dMaj = major;
        unsigned int dMin = minor;
        char link[PATH_MAX];
        snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", major, minor);
        char real[PATH_MAX];
        if (realpath(link, real) != nullptr)
        {
            //  Walk back to the disk basename: if a "partition" file exists
            //  in the realpath dir, the entry is a partition node and the
            //  parent dir is the whole disk.
            char* slash = strrchr(real, '/');
            const char* nm = slash ? slash + 1 : real;
            char partFile[PATH_MAX];
            snprintf(partFile, sizeof(partFile), "%s/partition", real);
            struct stat st;
            if (stat(partFile, &st) == 0 && slash)
            {
                *slash = '\0';
                slash = strrchr(real, '/');
                nm = slash ? slash + 1 : real;
            }
            const size_t cchNm = strlen(nm);
            if (cchNm > 0 && cchNm + 1 <= sizeof(diskName))
            {
                memcpy(diskName, nm, cchNm + 1);

                //  Parent disk's (major:minor) from /sys/block/<name>/dev.
                char devFile[PATH_MAX];
                snprintf(devFile, sizeof(devFile), "/sys/block/%s/dev", diskName);
                const int devFd = open(devFile, O_RDONLY | O_CLOEXEC);
                if (devFd >= 0)
                {
                    char buf[32];
                    const ssize_t n = read(devFd, buf, sizeof(buf) - 1);
                    close(devFd);
                    if (n > 0)
                    {
                        buf[n] = '\0';
                        sscanf(buf, "%u:%u", &dMaj, &dMin);
                    }
                }
            }
        }

        char* const ownedName = diskName[0] ? strdup(diskName) : nullptr;
        return osposix::KToHandle(
            new osposix::BlockDeviceObject(major, minor, dMaj, dMin, ownedName));
    }

    static constexpr char c_drivePrefixWin[]   = "\\\\.\\PHYSICALDRIVE";
    static constexpr char c_drivePrefixUnix[]  = "//./PHYSICALDRIVE";
    const char* driveSuffix = nullptr;
    if (HasPrefixI(path, c_drivePrefixWin))
        driveSuffix = path + PrefixLen(c_drivePrefixWin);
    else if (HasPrefixI(path, c_drivePrefixUnix))
        driveSuffix = path + PrefixLen(c_drivePrefixUnix);
    if (driveSuffix)
    {
        //  Engine asks for a whole-disk handle by index.  The DiskNumber
        //  the engine quotes here is what our IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
        //  shim packed via makedev(diskMajor, diskMinor) — see
        //  winapi_blockdev_ioctl.cxx.  Decode it the same way so this
        //  handle binds to the same disk identity the engine learned about.
        unsigned int packed = 0;
        if (sscanf(driveSuffix, "%u", &packed) != 1)
        {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        const dev_t dev      = (dev_t) packed;
        const unsigned int diskMajor = major(dev);
        const unsigned int diskMinor = minor(dev);

        //  Require a real /sys/dev/block backing — match Windows where
        //  CreateFileW(L"\\.\PhysicalDriveN") fails for non-existent N.
        //  Anonymous block devs (tmpfs/zfs/nfs) hit this branch too; engine
        //  treats the open failure as "couldn't determine disk identity"
        //  (FSeekPenalty defaults to HDD-flavoured).  Engine unit tests
        //  that construct COSDisk objects with synthetic dwDiskNumber
        //  values rely on this failure path.
        char link[PATH_MAX];
        snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", diskMajor, diskMinor);
        char real[PATH_MAX];
        if (realpath(link, real) == nullptr)
        {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }

        //  Walk back to the whole-disk name + canonical (major:minor).
        char diskNameLocal[64] = "";
        unsigned int dMaj = diskMajor;
        unsigned int dMin = diskMinor;
        {
            char* slash = strrchr(real, '/');
            const char* nm = slash ? slash + 1 : real;
            char partFile[PATH_MAX];
            snprintf(partFile, sizeof(partFile), "%s/partition", real);
            struct stat st;
            if (stat(partFile, &st) == 0 && slash)
            {
                *slash = '\0';
                slash = strrchr(real, '/');
                nm = slash ? slash + 1 : real;
            }
            const size_t cchNm = strlen(nm);
            if (cchNm > 0 && cchNm + 1 <= sizeof(diskNameLocal))
            {
                memcpy(diskNameLocal, nm, cchNm + 1);
                char devFile[PATH_MAX];
                snprintf(devFile, sizeof(devFile), "/sys/block/%s/dev", diskNameLocal);
                const int devFd = open(devFile, O_RDONLY | O_CLOEXEC);
                if (devFd >= 0)
                {
                    char buf[32];
                    const ssize_t n = read(devFd, buf, sizeof(buf) - 1);
                    close(devFd);
                    if (n > 0)
                    {
                        buf[n] = '\0';
                        sscanf(buf, "%u:%u", &dMaj, &dMin);
                    }
                }
            }
        }

        char* const ownedName = diskNameLocal[0] ? strdup(diskNameLocal) : nullptr;
        return osposix::KToHandle(
            new osposix::BlockDeviceObject(diskMajor, diskMinor, dMaj, dMin, ownedName));
    }

    return nullptr;  //  not a synthetic path; caller falls through to open()
}

} // anonymous namespace

extern "C"
{

HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES /*lpSecurityAttributes*/, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE /*hTemplateFile*/)
{
    char path[c_pathBuf];
    if (WidePathToUtf8(lpFileName, path, sizeof(path)) <= 0)
    {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }

    //  Synthetic Win32 device paths bypass the open() flow.  Engine
    //  normalizes path delimiters to chPathDelimiter ('/' on Linux), so
    //  the prefix arrives as either `\\?\` or `//?/`.
    const bool fUnc = (path[0] == '\\' && path[1] == '\\')
                   || (path[0] == '/'  && path[1] == '/');
    if (fUnc)
    {
        const HANDLE h = OpenSyntheticBlockDevice(path);
        if (h != nullptr)
            return h;
    }

    const int oflags = OpenFlagsFromWin32(dwDesiredAccess, dwShareMode,
        dwCreationDisposition, dwFlagsAndAttributes);
    const mode_t mode = 0644;
    const int fd = open(path, oflags, mode);
    if (fd < 0)
    {
        SetLastError(errno == ENOENT
                     ? ERROR_FILE_NOT_FOUND
                     : errno == EEXIST
                       ? ERROR_FILE_EXISTS
                       : errno == EACCES
                         ? ERROR_ACCESS_DENIED
                         : ERROR_OPEN_FAILED);
        return INVALID_HANDLE_VALUE;
    }

    // Apply Win32-share-mode-equivalent advisory locking. Non-blocking so
    // a contended file fails with ERROR_SHARING_VIOLATION rather than
    // hanging. Writers take LOCK_EX (deny everyone else); readers take
    // LOCK_SH (compatible with other LOCK_SH holders, fails against
    // LOCK_EX). FILE_SHARE_WRITE bypasses both — caller is explicitly
    // tolerating arbitrary other access.
    if ((dwShareMode & FILE_SHARE_WRITE) == 0)
    {
        const bool wantWrite = (dwDesiredAccess & GENERIC_WRITE) != 0;
        const int op = (wantWrite
                        ? LOCK_EX
                        : LOCK_SH) | LOCK_NB;
        if (flock(fd, op) != 0)
        {
            const int saved_errno = errno;
            close(fd);
            SetLastError(saved_errno == EWOULDBLOCK
                         ? ERROR_SHARING_VIOLATION
                         : ERROR_OPEN_FAILED);
            return INVALID_HANDLE_VALUE;
        }
    }

    return KToHandle(new FileObject(fd, dwFlagsAndAttributes, dwDesiredAccess,
        path, (dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0));
}

//  GetOverlappedResult — the engine pairs this with DeviceIoControl /
//  ReadFile / WriteFile when those return ERROR_IO_PENDING.  Our shimmed
//  versions are all synchronous (they fully complete or fail by the time
//  they return), so this just reports success.  bWait is irrelevant.
BOOL GetOverlappedResult(HANDLE /*hFile*/, LPOVERLAPPED lpOverlapped,
    LPDWORD lpNumberOfBytesTransferred, BOOL /*bWait*/)
{
    if (lpNumberOfBytesTransferred)
        *lpNumberOfBytesTransferred = lpOverlapped ? lpOverlapped->InternalHigh : 0;
    return TRUE;
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
    LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = 0;
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    ssize_t n;
    do
    {
        if (lpOverlapped)
        {
            const off_t off = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32)
                | lpOverlapped->Offset;
            n = pread(k->Fd(), lpBuffer, nNumberOfBytesToRead, off);
        }
        else
        {
            n = read(k->Fd(), lpBuffer, nNumberOfBytesToRead);
        }
    } while (n < 0 && errno == EINTR);
    if (n < 0)
    {
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    if (lpNumberOfBytesRead)
        *lpNumberOfBytesRead = static_cast<DWORD>(n);
    return TRUE;
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    ssize_t n;
    do
    {
        if (lpOverlapped)
        {
            const off_t off = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32)
                | lpOverlapped->Offset;
            n = pwrite(k->Fd(), lpBuffer, nNumberOfBytesToWrite, off);
        }
        else
        {
            n = write(k->Fd(), lpBuffer, nNumberOfBytesToWrite);
        }
    } while (n < 0 && errno == EINTR);
    if (n < 0)
    {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = static_cast<DWORD>(n);
    return TRUE;
}

// Win32 ReadFileScatter / WriteFileGather pass an array terminated by a
// NULL Buffer. Each element is exactly one VM page. We translate to
// preadv/pwritev with one iovec per element using sysconf for page size.
BOOL ReadFileScatter(HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
    DWORD nNumberOfBytesToRead, LPDWORD /*lpReserved*/, LPOVERLAPPED lpOverlapped)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k || !aSegmentArray || !lpOverlapped)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const long pageSz = sysconf(_SC_PAGESIZE);
    constexpr int c_maxIov = 64;
    iovec iov[c_maxIov];
    int nIov = 0;
    DWORD remaining = nNumberOfBytesToRead;
    for (int i = 0; aSegmentArray[i].Buffer != nullptr && nIov < c_maxIov && remaining; ++i)
    {
        const DWORD chunk = remaining < (DWORD) pageSz
                            ? remaining
                            : (DWORD) pageSz;
        iov[nIov].iov_base = aSegmentArray[i].Buffer;
        iov[nIov].iov_len = chunk;
        remaining -= chunk;
        ++nIov;
    }
    const off_t off = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset;
    ssize_t n;
    do
    {
        n = preadv(k->Fd(), iov, nIov, off);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
    {
        SetLastError(ERROR_READ_FAULT);
        return FALSE;
    }
    return TRUE;
}

BOOL WriteFileGather(HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
    DWORD nNumberOfBytesToWrite, LPDWORD /*lpReserved*/, LPOVERLAPPED lpOverlapped)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k || !aSegmentArray || !lpOverlapped)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const long pageSz = sysconf(_SC_PAGESIZE);
    constexpr int c_maxIov = 64;
    iovec iov[c_maxIov];
    int nIov = 0;
    DWORD remaining = nNumberOfBytesToWrite;
    for (int i = 0; aSegmentArray[i].Buffer != nullptr && nIov < c_maxIov && remaining; ++i)
    {
        const DWORD chunk = remaining < (DWORD) pageSz
                            ? remaining
                            : (DWORD) pageSz;
        iov[nIov].iov_base = aSegmentArray[i].Buffer;
        iov[nIov].iov_len = chunk;
        remaining -= chunk;
        ++nIov;
    }
    const off_t off = (static_cast<off_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset;
    ssize_t n;
    do
    {
        n = pwritev(k->Fd(), iov, nIov, off);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
    {
        SetLastError(ERROR_WRITE_FAULT);
        return FALSE;
    }
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE hFile)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return (fdatasync(k->Fd()) == 0)
           ? TRUE
           : FALSE;
}

BOOL GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k || !lpFileSize)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    struct stat st;
    if (fstat(k->Fd(), &st) < 0)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    lpFileSize->QuadPart = st.st_size;
    return TRUE;
}

BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
    PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    int whence;
    switch (dwMoveMethod)
    {
        case FILE_BEGIN:
            whence = SEEK_SET;
            break;
        case FILE_CURRENT:
            whence = SEEK_CUR;
            break;
        case FILE_END:
            whence = SEEK_END;
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }
    const off_t pos = lseek(k->Fd(), liDistanceToMove.QuadPart, whence);
    if (pos == (off_t) -1)
    {
        SetLastError(ERROR_SEEK);
        return FALSE;
    }
    if (lpNewFilePointer)
        lpNewFilePointer->QuadPart = pos;
    return TRUE;
}

BOOL SetEndOfFile(HANDLE hFile)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const off_t pos = lseek(k->Fd(), 0, SEEK_CUR);
    if (pos == (off_t) -1)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    struct stat st;
    if (fstat(k->Fd(), &st) != 0)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    // Win32 SetEndOfFile sets EOF to the current file pointer. When that grows
    // the file, ALLOCATE the new region instead of leaving an ftruncate hole:
    // the engine's ErrSetSize takes its fast extend path on the assumption that a
    // non-fmfSparse file's extended region is backed by allocated storage (NTFS
    // SetEndOfFile reserves clusters). On Linux ftruncate would make it sparse,
    // and a page flushed into a hole and later evicted reads back as zeros, which
    // the engine reports as JET_errPageNotInitialized. posix_fallocate reserves
    // [st_size, pos) without zero-fill IO (allocated blocks read as zero until
    // written) and grows the file to pos.
    if (pos > st.st_size)
    {
        const int rc = posix_fallocate(k->Fd(), st.st_size, pos - st.st_size);
        if (rc != 0)
        {
            SetLastError(rc == ENOSPC ? ERROR_DISK_FULL : ERROR_IO_DEVICE);
            return FALSE;
        }
        return TRUE;
    }

    // Same size or shrink: truncate to the exact size (releases any blocks past pos).
    return (ftruncate(k->Fd(), pos) == 0)
           ? TRUE
           : FALSE;
}

// SetFileValidData on Windows extends the valid data length without
// zero-fill (security-elevated on NTFS). Linux has no portable equivalent;
// engine treats failure as "fall back to zero-fill", which is fine — we
// simply succeed and let posix_fallocate/ftruncate handle extension.
BOOL SetFileValidData(HANDLE hFile, LONGLONG /*ValidDataLength*/)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL GetFileInformationByHandle(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
{
    FileObject* const k = As<FileObject>(hFile);
    if (!k || !lpFileInformation)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    struct stat st;
    if (fstat(k->Fd(), &st) < 0)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    memset(lpFileInformation, 0, sizeof(*lpFileInformation));
    lpFileInformation->dwFileAttributes = AttributesFromMode(st.st_mode);
    lpFileInformation->ftCreationTime = TimespecToFileTime(st.st_ctim);
    lpFileInformation->ftLastAccessTime = TimespecToFileTime(st.st_atim);
    lpFileInformation->ftLastWriteTime = TimespecToFileTime(st.st_mtim);
    lpFileInformation->dwVolumeSerialNumber = static_cast<DWORD>(st.st_dev);
    lpFileInformation->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32);
    lpFileInformation->nFileSizeLow = static_cast<DWORD>(st.st_size & 0xFFFFFFFF);
    lpFileInformation->nNumberOfLinks = static_cast<DWORD>(st.st_nlink);
    lpFileInformation->nFileIndexHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_ino) >> 32);
    lpFileInformation->nFileIndexLow = static_cast<DWORD>(st.st_ino & 0xFFFFFFFF);
    return TRUE;
}
} // extern "C"
