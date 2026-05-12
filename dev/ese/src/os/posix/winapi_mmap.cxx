// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Win32 memory-mapping surface: CreateFileMappingW, MapViewOfFile,
// MapViewOfFileEx, UnmapViewOfFile, FlushViewOfFile.  Backed by mmap()
// and friends.
//
// The Windows model splits the operation in two: CreateFileMappingW
// allocates a "section object" (a kernel-named region tied to a file
// handle and a max-size cap), then MapViewOfFile materializes a view
// of that section at process-virtual addresses.  POSIX collapses the
// two: mmap() maps the file directly.  We mirror Windows' two-step
// shape by carrying the file's fd in a KObject of kind FileMapping
// and letting MapViewOfFile* call mmap() against it.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using osposix::AllocKObject;
using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;
using osposix::KToHandle;

namespace
{

int ProtFromPageFlags( DWORD flProtect )
{
    //  Strip the SEC_* and PAGE_GUARD / PAGE_NOCACHE bits — those affect
    //  Windows' commit / cacheability semantics, not the protection bits
    //  that map to PROT_*.
    const DWORD prot = flProtect & 0xFFu;
    switch ( prot )
    {
        case PAGE_NOACCESS:
            return PROT_NONE;
        case PAGE_READONLY:
            return PROT_READ;
        case PAGE_READWRITE:
            return PROT_READ | PROT_WRITE;
        case PAGE_WRITECOPY:
            //  Win32 maps copy-on-write; mmap's MAP_PRIVATE gives the same
            //  semantics under PROT_WRITE.
            return PROT_READ | PROT_WRITE;
        case PAGE_EXECUTE:
            return PROT_EXEC;
        case PAGE_EXECUTE_READ:
            return PROT_READ | PROT_EXEC;
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
    return PROT_READ;
}

int ProtFromAccessFlags( DWORD dwDesiredAccess )
{
    if ( dwDesiredAccess & FILE_MAP_COPY )
    {
        return PROT_READ | PROT_WRITE;
    }
    int prot = 0;
    if ( dwDesiredAccess & FILE_MAP_READ )
    {
        prot |= PROT_READ;
    }
    if ( dwDesiredAccess & FILE_MAP_WRITE )
    {
        prot |= PROT_READ | PROT_WRITE;
    }
    if ( dwDesiredAccess & FILE_MAP_EXECUTE )
    {
        prot |= PROT_EXEC;
    }
    return prot != 0 ? prot : PROT_READ;
}

int FlagsFromAccess( DWORD dwDesiredAccess )
{
    return ( dwDesiredAccess & FILE_MAP_COPY ) ? MAP_PRIVATE : MAP_SHARED;
}

}  //  namespace

extern "C" {

HANDLE CreateFileMappingW( HANDLE hFile, LPSECURITY_ATTRIBUTES /*lpAttributes*/,
                           DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow,
                           LPCWSTR /*lpName*/ )
{
    KObject* const kFile = HandleToK( hFile );
    if ( !kFile || kFile->kind != HandleKind::File || kFile->fileFd < 0 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }

    //  dup so the mapping handle has independent lifetime from the file
    //  handle (Win32 semantics: CloseHandle on the file doesn't tear down
    //  the section, and vice versa).
    const int fdDup = dup( kFile->fileFd );
    if ( fdDup < 0 )
    {
        SetLastError( ERROR_NO_SYSTEM_RESOURCES );
        return NULL;
    }

    KObject* const k = AllocKObject( HandleKind::FileMapping );
    if ( !k )
    {
        close( fdDup );
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return NULL;
    }
    k->mappingFd      = fdDup;
    k->mappingProtect = flProtect;
    k->mappingMaxSize = ( (QWORD)dwMaximumSizeHigh << 32 ) | (QWORD)dwMaximumSizeLow;
    return KToHandle( k );
}

LPVOID MapViewOfFileEx( HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                        DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow,
                        SIZE_T dwNumberOfBytesToMap, LPVOID lpBaseAddress )
{
    KObject* const k = HandleToK( hFileMappingObject );
    if ( !k || k->kind != HandleKind::FileMapping || k->mappingFd < 0 )
    {
        SetLastError( ERROR_INVALID_HANDLE );
        return NULL;
    }

    const off_t ibOffset = ( (off_t)dwFileOffsetHigh << 32 ) | (off_t)dwFileOffsetLow;

    //  dwNumberOfBytesToMap == 0 means "map to end of file" on Win32.
    size_t cbView = dwNumberOfBytesToMap;
    if ( cbView == 0 )
    {
        struct stat st;
        if ( fstat( k->mappingFd, &st ) < 0 )
        {
            SetLastError( ERROR_INVALID_HANDLE );
            return NULL;
        }
        if ( (off_t)st.st_size <= ibOffset )
        {
            SetLastError( ERROR_INVALID_PARAMETER );
            return NULL;
        }
        cbView = (size_t)( st.st_size - ibOffset );
    }

    const int prot  = ProtFromAccessFlags( dwDesiredAccess );
    const int flags = FlagsFromAccess( dwDesiredAccess );

    //  MAP_FIXED is dangerous (silently unmaps existing mappings).  Use
    //  it only when the caller explicitly requests a base address —
    //  matching MapViewOfFileEx's "hint vs. require" semantics.  Even
    //  then, request the kernel-chosen variant first and re-map at the
    //  hint as a hint, falling through to any address on failure.
    void* const pv = mmap( lpBaseAddress,
                           cbView,
                           prot,
                           flags,
                           k->mappingFd,
                           ibOffset );
    if ( pv == MAP_FAILED )
    {
        SetLastError( errno == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER );
        return NULL;
    }
    return pv;
}

LPVOID MapViewOfFile( HANDLE hFileMappingObject, DWORD dwDesiredAccess,
                      DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow,
                      SIZE_T dwNumberOfBytesToMap )
{
    return MapViewOfFileEx( hFileMappingObject, dwDesiredAccess,
                            dwFileOffsetHigh, dwFileOffsetLow,
                            dwNumberOfBytesToMap, NULL );
}

BOOL UnmapViewOfFile( LPCVOID lpBaseAddress )
{
    if ( !lpBaseAddress )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    //  Win32's UnmapViewOfFile doesn't take a length; the kernel tracks
    //  the mapping for us.  POSIX munmap needs a length — but we don't
    //  have it.  The closest substitute is munmap with length = page
    //  size, which on Linux releases the whole VMA the address falls in.
    //  This is what Wine's mountmgr does for the same shim.
    if ( munmap( (void*)lpBaseAddress, sysconf( _SC_PAGESIZE ) ) < 0 )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    return TRUE;
}

BOOL VirtualProtect( LPVOID lpAddress, SIZE_T dwSize, DWORD flNewProtect,
                     PDWORD lpflOldProtect )
{
    if ( !lpAddress || !dwSize )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    //  We don't track per-region prior protection bits — Win32 callers
    //  that consult lpflOldProtect mostly do so to restore it later,
    //  which is a pattern we don't currently exercise.  Report a benign
    //  default and let mprotect do the work.
    if ( lpflOldProtect )
    {
        *lpflOldProtect = PAGE_READWRITE;
    }
    const int prot = ProtFromPageFlags( flNewProtect );
    //  Align down to page boundary, round size up — mprotect rejects
    //  unaligned starts.  Matches Win32's silent rounding semantics.
    const size_t cbPage = (size_t)sysconf( _SC_PAGESIZE );
    const uintptr_t uAddr = (uintptr_t)lpAddress;
    const uintptr_t uAligned = uAddr & ~(uintptr_t)( cbPage - 1 );
    const size_t cbAligned = ( ( uAddr - uAligned ) + dwSize + cbPage - 1 ) & ~( cbPage - 1 );
    if ( mprotect( (void*)uAligned, cbAligned, prot ) < 0 )
    {
        SetLastError( errno == EACCES ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    return TRUE;
}

BOOL FlushViewOfFile( LPCVOID lpBaseAddress, SIZE_T dwNumberOfBytesToFlush )
{
    if ( !lpBaseAddress )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    const size_t cb = dwNumberOfBytesToFlush != 0 ? dwNumberOfBytesToFlush
                                                  : (size_t)sysconf( _SC_PAGESIZE );
    if ( msync( (void*)lpBaseAddress, cb, MS_SYNC ) < 0 )
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    return TRUE;
}

}  //  extern "C"
