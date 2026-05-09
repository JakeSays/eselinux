// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// CreateFileW / ReadFile / WriteFile / scatter-gather / size / seek /
// truncate / flush / GetFileInformationByHandle. Backs a Win32 file
// HANDLE with an extended KObject (kind=File, fileFd holds the OS fd).
//
// OVERLAPPED handling here is synchronous — pread/pwrite use the offset
// in OVERLAPPED but do not signal any event or schedule async completion.
// The engine wraps real async work through the io_uring task layer
// (winapi_task.cxx, future); this file is the simple inline path.
//
// FILE_FLAG_NO_BUFFERING maps to O_DIRECT (kernel honors alignment
// requirements). FILE_FLAG_WRITE_THROUGH maps to O_SYNC. FILE_FLAG_
// DELETE_ON_CLOSE is recorded in fileDeleteOnClosePath and the unlink
// happens in FreeKObject.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>   // flock
#include <sys/uio.h>
#include <unistd.h>

using osposix::AllocKObject;
using osposix::FreeKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;
using osposix::Utf8ToWide;
using osposix::WidePathToUtf8;

namespace
{
    // FILETIME = 100-ns ticks since 1601-01-01 UTC; UNIX epoch offset.
    constexpr uint64_t c_filetimeUnixEpochOffset = 11644473600ULL;
    constexpr uint64_t c_filetimeIntervalsPerSec = 10000000ULL;

    FILETIME TimespecToFileTime( const struct timespec& ts )
    {
        const uint64_t total = ( static_cast<uint64_t>( ts.tv_sec ) + c_filetimeUnixEpochOffset )
                                   * c_filetimeIntervalsPerSec
                               + static_cast<uint64_t>( ts.tv_nsec ) / 100;
        FILETIME ft;
        ft.dwLowDateTime  = static_cast<DWORD>( total & 0xFFFFFFFF );
        ft.dwHighDateTime = static_cast<DWORD>( total >> 32 );
        return ft;
    }

    int OpenFlagsFromWin32( DWORD desiredAccess, DWORD shareMode, DWORD creation, DWORD flags )
    {
        int oflags = 0;
        const bool wantRead  = ( desiredAccess & GENERIC_READ )  != 0;
        const bool wantWrite = ( desiredAccess & GENERIC_WRITE ) != 0;
        if ( wantRead && wantWrite )      oflags = O_RDWR;
        else if ( wantWrite )             oflags = O_WRONLY;
        else                              oflags = O_RDONLY;

        switch ( creation )
        {
            case CREATE_NEW:        oflags |= O_CREAT | O_EXCL; break;
            case CREATE_ALWAYS:     oflags |= O_CREAT | O_TRUNC; break;
            case OPEN_EXISTING:     break;
            case OPEN_ALWAYS:       oflags |= O_CREAT; break;
            case TRUNCATE_EXISTING: oflags |= O_TRUNC; break;
        }

        if ( flags & FILE_FLAG_NO_BUFFERING ) oflags |= O_DIRECT;
        if ( flags & FILE_FLAG_WRITE_THROUGH ) oflags |= O_SYNC;
        oflags |= O_CLOEXEC;
        ( void )shareMode;
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
    int FlockModeFromShareMode( DWORD shareMode )
    {
        if ( ( shareMode & FILE_SHARE_WRITE ) != 0 )
        {
            return 0;
        }
        return LOCK_EX;
    }

    DWORD AttributesFromMode( mode_t m )
    {
        DWORD attrs = 0;
        if ( S_ISDIR( m ) ) attrs |= FILE_ATTRIBUTE_DIRECTORY;
        if ( ( m & S_IWUSR ) == 0 ) attrs |= FILE_ATTRIBUTE_READONLY;
        if ( attrs == 0 ) attrs = FILE_ATTRIBUTE_NORMAL;
        return attrs;
    }
}

extern "C" {

HANDLE CreateFileW( LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                    LPSECURITY_ATTRIBUTES /*lpSecurityAttributes*/, DWORD dwCreationDisposition,
                    DWORD dwFlagsAndAttributes, HANDLE /*hTemplateFile*/ )
{
    char path[ 4096 ];
    if ( WidePathToUtf8( lpFileName, path, sizeof( path ) ) <= 0 )
    {
        SetLastError( ERROR_FILE_NOT_FOUND );
        return INVALID_HANDLE_VALUE;
    }

    const int oflags = OpenFlagsFromWin32( dwDesiredAccess, dwShareMode,
                                           dwCreationDisposition, dwFlagsAndAttributes );
    const mode_t mode = 0644;
    const int fd = open( path, oflags, mode );
    if ( fd < 0 )
    {
        SetLastError( errno == ENOENT ? ERROR_FILE_NOT_FOUND :
                      errno == EEXIST ? ERROR_FILE_EXISTS :
                      errno == EACCES ? ERROR_ACCESS_DENIED :
                                        ERROR_OPEN_FAILED );
        return INVALID_HANDLE_VALUE;
    }

    // Apply Win32-share-mode-equivalent advisory locking. Non-blocking so
    // a contended file fails with ERROR_SHARING_VIOLATION rather than
    // hanging. Writers take LOCK_EX (deny everyone else); readers take
    // LOCK_SH (compatible with other LOCK_SH holders, fails against
    // LOCK_EX). FILE_SHARE_WRITE bypasses both — caller is explicitly
    // tolerating arbitrary other access.
    if ( ( dwShareMode & FILE_SHARE_WRITE ) == 0 )
    {
        const bool wantWrite = ( dwDesiredAccess & GENERIC_WRITE ) != 0;
        const int op = ( wantWrite ? LOCK_EX : LOCK_SH ) | LOCK_NB;
        if ( flock( fd, op ) != 0 )
        {
            const int saved_errno = errno;
            close( fd );
            SetLastError( saved_errno == EWOULDBLOCK ? ERROR_SHARING_VIOLATION :
                                                       ERROR_OPEN_FAILED );
            return INVALID_HANDLE_VALUE;
        }
    }

    KObject* const k = AllocKObject( HandleKind::File );
    if ( !k )
    {
        close( fd );
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return INVALID_HANDLE_VALUE;
    }
    k->fileFd            = fd;
    k->fileFlagsAndAttrs = dwFlagsAndAttributes;
    k->fileDesiredAccess = dwDesiredAccess;
    k->fileOpenedPath    = strdup( path );
    if ( dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE )
    {
        k->fileDeleteOnClosePath = strdup( path );
    }
    return KToHandle( k );
}

BOOL ReadFile( HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
               LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped )
{
    if ( lpNumberOfBytesRead ) *lpNumberOfBytesRead = 0;
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    ssize_t n;
    if ( lpOverlapped )
    {
        const off_t off = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 )
                            | lpOverlapped->Offset;
        n = pread( k->fileFd, lpBuffer, nNumberOfBytesToRead, off );
    }
    else
    {
        n = read( k->fileFd, lpBuffer, nNumberOfBytesToRead );
    }
    if ( n < 0 )
    {
        SetLastError( ERROR_READ_FAULT );
        return FALSE;
    }
    if ( lpNumberOfBytesRead ) *lpNumberOfBytesRead = static_cast<DWORD>( n );
    return TRUE;
}

BOOL WriteFile( HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped )
{
    if ( lpNumberOfBytesWritten ) *lpNumberOfBytesWritten = 0;
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }

    ssize_t n;
    if ( lpOverlapped )
    {
        const off_t off = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 )
                            | lpOverlapped->Offset;
        n = pwrite( k->fileFd, lpBuffer, nNumberOfBytesToWrite, off );
    }
    else
    {
        n = write( k->fileFd, lpBuffer, nNumberOfBytesToWrite );
    }
    if ( n < 0 )
    {
        SetLastError( ERROR_WRITE_FAULT );
        return FALSE;
    }
    if ( lpNumberOfBytesWritten ) *lpNumberOfBytesWritten = static_cast<DWORD>( n );
    return TRUE;
}

// Win32 ReadFileScatter / WriteFileGather pass an array terminated by a
// NULL Buffer. Each element is exactly one VM page. We translate to
// preadv/pwritev with one iovec per element using sysconf for page size.
BOOL ReadFileScatter( HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
                      DWORD nNumberOfBytesToRead, LPDWORD /*lpReserved*/, LPOVERLAPPED lpOverlapped )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !aSegmentArray || !lpOverlapped )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    const long pageSz = sysconf( _SC_PAGESIZE );
    constexpr int c_maxIov = 64;
    iovec iov[ c_maxIov ];
    int nIov = 0;
    DWORD remaining = nNumberOfBytesToRead;
    for ( int i = 0; aSegmentArray[ i ].Buffer != nullptr && nIov < c_maxIov && remaining; ++i )
    {
        const DWORD chunk = remaining < (DWORD)pageSz ? remaining : (DWORD)pageSz;
        iov[ nIov ].iov_base = aSegmentArray[ i ].Buffer;
        iov[ nIov ].iov_len  = chunk;
        remaining -= chunk;
        ++nIov;
    }
    const off_t off = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 ) | lpOverlapped->Offset;
    const ssize_t n = preadv( k->fileFd, iov, nIov, off );
    if ( n < 0 )
    {
        SetLastError( ERROR_READ_FAULT );
        return FALSE;
    }
    return TRUE;
}

BOOL WriteFileGather( HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
                      DWORD nNumberOfBytesToWrite, LPDWORD /*lpReserved*/, LPOVERLAPPED lpOverlapped )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !aSegmentArray || !lpOverlapped )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    const long pageSz = sysconf( _SC_PAGESIZE );
    constexpr int c_maxIov = 64;
    iovec iov[ c_maxIov ];
    int nIov = 0;
    DWORD remaining = nNumberOfBytesToWrite;
    for ( int i = 0; aSegmentArray[ i ].Buffer != nullptr && nIov < c_maxIov && remaining; ++i )
    {
        const DWORD chunk = remaining < (DWORD)pageSz ? remaining : (DWORD)pageSz;
        iov[ nIov ].iov_base = aSegmentArray[ i ].Buffer;
        iov[ nIov ].iov_len  = chunk;
        remaining -= chunk;
        ++nIov;
    }
    const off_t off = ( static_cast<off_t>( lpOverlapped->OffsetHigh ) << 32 ) | lpOverlapped->Offset;
    const ssize_t n = pwritev( k->fileFd, iov, nIov, off );
    if ( n < 0 )
    {
        SetLastError( ERROR_WRITE_FAULT );
        return FALSE;
    }
    return TRUE;
}

BOOL FlushFileBuffers( HANDLE hFile )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return ( fdatasync( k->fileFd ) == 0 ) ? TRUE : FALSE;
}

BOOL GetFileSizeEx( HANDLE hFile, PLARGE_INTEGER lpFileSize )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !lpFileSize )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    struct stat st;
    if ( fstat( k->fileFd, &st ) < 0 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    lpFileSize->QuadPart = st.st_size;
    return TRUE;
}

BOOL SetFilePointerEx( HANDLE hFile, LARGE_INTEGER liDistanceToMove,
                       PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    int whence;
    switch ( dwMoveMethod )
    {
        case FILE_BEGIN:   whence = SEEK_SET; break;
        case FILE_CURRENT: whence = SEEK_CUR; break;
        case FILE_END:     whence = SEEK_END; break;
        default:
            SetLastError( ERROR_INVALID_PARAMETER );
            return FALSE;
    }
    const off_t pos = lseek( k->fileFd, liDistanceToMove.QuadPart, whence );
    if ( pos == (off_t)-1 )
    {
        SetLastError( ERROR_SEEK );
        return FALSE;
    }
    if ( lpNewFilePointer ) lpNewFilePointer->QuadPart = pos;
    return TRUE;
}

BOOL SetEndOfFile( HANDLE hFile )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    const off_t pos = lseek( k->fileFd, 0, SEEK_CUR );
    if ( pos == (off_t)-1 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return ( ftruncate( k->fileFd, pos ) == 0 ) ? TRUE : FALSE;
}

// SetFileValidData on Windows extends the valid data length without
// zero-fill (security-elevated on NTFS). Linux has no portable equivalent;
// engine treats failure as "fall back to zero-fill", which is fine — we
// simply succeed and let posix_fallocate/ftruncate handle extension.
BOOL SetFileValidData( HANDLE hFile, LONGLONG /*ValidDataLength*/ )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    return TRUE;
}

BOOL GetFileInformationByHandle( HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation )
{
    KObject* const k = HandleToK( hFile );
    if ( !k || k->kind != HandleKind::File || !lpFileInformation )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    struct stat st;
    if ( fstat( k->fileFd, &st ) < 0 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return FALSE;
    }
    memset( lpFileInformation, 0, sizeof( *lpFileInformation ) );
    lpFileInformation->dwFileAttributes     = AttributesFromMode( st.st_mode );
    lpFileInformation->ftCreationTime       = TimespecToFileTime( st.st_ctim );
    lpFileInformation->ftLastAccessTime     = TimespecToFileTime( st.st_atim );
    lpFileInformation->ftLastWriteTime      = TimespecToFileTime( st.st_mtim );
    lpFileInformation->dwVolumeSerialNumber = static_cast<DWORD>( st.st_dev );
    lpFileInformation->nFileSizeHigh        = static_cast<DWORD>( static_cast<uint64_t>( st.st_size ) >> 32 );
    lpFileInformation->nFileSizeLow         = static_cast<DWORD>( st.st_size & 0xFFFFFFFF );
    lpFileInformation->nNumberOfLinks       = static_cast<DWORD>( st.st_nlink );
    lpFileInformation->nFileIndexHigh       = static_cast<DWORD>( static_cast<uint64_t>( st.st_ino ) >> 32 );
    lpFileInformation->nFileIndexLow        = static_cast<DWORD>( st.st_ino & 0xFFFFFFFF );
    return TRUE;
}

}  // extern "C"
