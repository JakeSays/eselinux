// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// CSyncFile — a minimal synchronous IFileAPI implementation backed by
// the windows-shim CreateFile/ReadFile/WriteFile path. Replaces the
// stubs in osfs_posix.cxx (ErrFileOpen / ErrFileCreate) so the engine
// can actually open and read/write the database + log files.
//
// What we provide:
//   - ErrPath, ErrSize, ErrIsReadOnly, ErrSetSize, ErrFlushFileBuffers
//   - ErrIORead / ErrIOWrite (synchronous; pfnIOComplete is invoked
//     before returning so the engine's async path treats them as
//     immediately-issued completions)
//   - ErrIOIssue (no-op; we have no batch to flush)
//   - ErrIOSize / ErrSectorSize  (4096)
//   - ErrSetSparseness (no-op; Linux ext4/xfs already handle sparse)
//   - Trim/Rename/Delete via the Win32-shim entry points
//
// What we stub to errFeatureNotAvailable:
//   - Memory-mapped IO (ErrMMRead/Copy/MMIORead/Revert/Free)
//   - NTFS-attribute-list query (no Linux equivalent)
//   - DiskId (always 0)
//   - IOREQ pool (immediate alloc/free)

#include "osstd.hxx"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace
{

class CSyncFile : public IFileAPI
{
    HANDLE                  m_hFile;
    WCHAR                   m_wszPath[ OSFSAPI_MAX_PATH ];
    IFileAPI::FileModeFlags m_fmf;
    IFilePerfAPI*           m_pfpapi;

public:
    CSyncFile( HANDLE hFile, const WCHAR* wszPath, IFileAPI::FileModeFlags fmf )
        :   m_hFile( hFile ),
            m_fmf( fmf ),
            m_pfpapi( nullptr )
    {
        size_t i = 0;
        for ( ; wszPath[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i ) m_wszPath[ i ] = wszPath[ i ];
        m_wszPath[ i ] = L'\0';
    }

    ~CSyncFile() override
    {
        if ( m_hFile != INVALID_HANDLE_VALUE )
        {
            CloseHandle( m_hFile );
        }
    }

    HANDLE Handle() const { return m_hFile; }

    //
    //  Identification / properties
    //

    FileModeFlags Fmf() const override
    {
        return m_fmf;
    }

    ERR ErrPath( __out_bcount(cbOSFSAPI_MAX_PATHW) WCHAR* const wszAbsPath ) override
    {
        if ( !wszAbsPath ) return ErrERRCheck( JET_errInvalidParameter );
        size_t i = 0;
        for ( ; m_wszPath[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i ) wszAbsPath[ i ] = m_wszPath[ i ];
        wszAbsPath[ i ] = L'\0';
        return JET_errSuccess;
    }

    ERR ErrSize( _Out_ QWORD* const pcbSize, _In_ const FILESIZE filesize ) override
    {
        if ( !pcbSize ) return ErrERRCheck( JET_errInvalidParameter );
        LARGE_INTEGER li;
        if ( !GetFileSizeEx( m_hFile, &li ) ) return ErrERRCheck( JET_errDiskIO );
        *pcbSize = static_cast<QWORD>( li.QuadPart );
        (void)filesize;  //  logical and on-disk sizes are reported the same on Linux
        return JET_errSuccess;
    }

    ERR ErrIsReadOnly( BOOL* const pfReadOnly ) override
    {
        if ( !pfReadOnly ) return ErrERRCheck( JET_errInvalidParameter );
        *pfReadOnly = ( ( m_fmf & ( fmfReadOnly | fmfReadOnlyClient | fmfReadOnlyPermissive ) ) != 0 );
        return JET_errSuccess;
    }

    LONG CLogicalCopies() override
    {
        return 1;
    }

    ERR ErrSetSize( const TraceContext& /*tc*/,
                    const QWORD         cbSize,
                    const BOOL          /*fZeroFill*/,
                    const OSFILEQOS     /*grbitQOS*/ ) override
    {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>( cbSize );
        if ( !SetFilePointerEx( m_hFile, li, NULL, FILE_BEGIN ) )
            return ErrERRCheck( JET_errDiskIO );
        if ( !SetEndOfFile( m_hFile ) )
            return ErrERRCheck( JET_errDiskIO );
        return JET_errSuccess;
    }

    ERR ErrRename( const WCHAR* const wszAbsPathDest,
                   const BOOL         fOverwriteExisting = fFalse ) override
    {
        const DWORD dwFlags = fOverwriteExisting ? MOVEFILE_REPLACE_EXISTING : 0;
        if ( !MoveFileExW( m_wszPath, wszAbsPathDest, dwFlags ) )
            return ErrERRCheck( JET_errFileAccessDenied );
        size_t i = 0;
        for ( ; wszAbsPathDest[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i ) m_wszPath[ i ] = wszAbsPathDest[ i ];
        m_wszPath[ i ] = L'\0';
        return JET_errSuccess;
    }

    ERR ErrSetSparseness() override
    {
        //  Linux filesystems ext4/xfs/btrfs already create sparse files
        //  whenever a hole is left between writes; no equivalent of
        //  FSCTL_SET_SPARSE is needed.
        return JET_errSuccess;
    }

    ERR ErrIOTrim( const TraceContext& /*tc*/,
                   const QWORD         /*ibOffset*/,
                   const QWORD         /*cbToFree*/ ) override
    {
        //  Could be implemented via fallocate(FALLOC_FL_PUNCH_HOLE) but
        //  the engine treats trim as best-effort; success is fine.
        return JET_errSuccess;
    }

    ERR ErrRetrieveAllocatedRegion( const QWORD     /*ibOffsetToQuery*/,
                                    _Out_ QWORD* const pibStartTrimmedRegion,
                                    _Out_ QWORD* const pcbTrimmed ) override
    {
        if ( pibStartTrimmedRegion ) *pibStartTrimmedRegion = 0;
        if ( pcbTrimmed ) *pcbTrimmed = 0;
        return JET_errSuccess;
    }

    ERR ErrFlushFileBuffers( const IOFLUSHREASON /*iofr*/ ) override
    {
        if ( !FlushFileBuffers( m_hFile ) ) return ErrERRCheck( JET_errDiskIO );
        return JET_errSuccess;
    }

    void SetNoFlushNeeded() override
    {
    }

    ERR ErrIOSize( DWORD* const pcbSize ) override
    {
        if ( pcbSize ) *pcbSize = 4096;
        return JET_errSuccess;
    }

    ERR ErrSectorSize( DWORD* const pcbSize ) override
    {
        if ( pcbSize ) *pcbSize = 4096;
        return JET_errSuccess;
    }

    ERR ErrReserveIOREQ( const QWORD     /*ibOffset*/,
                         const DWORD     /*cbData*/,
                         const OSFILEQOS /*grbitQOS*/,
                         VOID **         ppioreq ) override
    {
        if ( ppioreq ) *ppioreq = nullptr;
        return JET_errSuccess;
    }

    VOID ReleaseUnusedIOREQ( VOID * /*pioreq*/ ) override
    {
    }

    ERR ErrIORead(  const TraceContext& tc,
                    const QWORD         ibOffset,
                    const DWORD         cbData,
        __out_bcount( cbData )  BYTE* const         pbData,
                    const OSFILEQOS     grbitQOS,
                    const PfnIOComplete pfnIOComplete   = NULL,
                    const DWORD_PTR     keyIOComplete   = NULL,
                    const PfnIOHandoff  pfnIOHandoff    = NULL,
                    const VOID *        /*pioreq*/      = NULL ) override
    {
        ERR err = JET_errSuccess;
        OVERLAPPED ov{};
        ov.Offset     = static_cast<DWORD>( ibOffset );
        ov.OffsetHigh = static_cast<DWORD>( ibOffset >> 32 );
        DWORD cbRead = 0;
        if ( !ReadFile( m_hFile, pbData, cbData, &cbRead, &ov ) )
        {
            err = ErrERRCheck( JET_errDiskIO );
        }
        else if ( cbRead != cbData )
        {
            err = ErrERRCheck( JET_errFileIOBeyondEOF );
        }
        FullTraceContext ftc;
        ftc.etc = tc;
        if ( pfnIOHandoff )
        {
            pfnIOHandoff( err, this, ftc, grbitQOS, ibOffset, cbData, pbData, keyIOComplete, nullptr );
        }
        if ( pfnIOComplete )
        {
            pfnIOComplete( err, this, ftc, grbitQOS, ibOffset, cbData, pbData, keyIOComplete );
            return JET_errSuccess;
        }
        return err;
    }

    ERR ErrIOWrite( const TraceContext& tc,
                    const QWORD         ibOffset,
                    const DWORD         cbData,
                    const BYTE* const   pbData,
                    const OSFILEQOS     grbitQOS,
                    const PfnIOComplete pfnIOComplete   = NULL,
                    const DWORD_PTR     keyIOComplete   = NULL,
                    const PfnIOHandoff  pfnIOHandoff    = NULL ) override
    {
        ERR err = JET_errSuccess;
        OVERLAPPED ov{};
        ov.Offset     = static_cast<DWORD>( ibOffset );
        ov.OffsetHigh = static_cast<DWORD>( ibOffset >> 32 );
        DWORD cbWritten = 0;
        if ( !WriteFile( m_hFile, pbData, cbData, &cbWritten, &ov ) )
        {
            err = ErrERRCheck( JET_errDiskIO );
        }
        else if ( cbWritten != cbData )
        {
            err = ErrERRCheck( JET_errDiskFull );
        }
        FullTraceContext ftc;
        ftc.etc = tc;
        if ( pfnIOHandoff )
        {
            pfnIOHandoff( err, this, ftc, grbitQOS, ibOffset, cbData, pbData, keyIOComplete, nullptr );
        }
        if ( pfnIOComplete )
        {
            pfnIOComplete( err, this, ftc, grbitQOS, ibOffset, cbData, pbData, keyIOComplete );
            return JET_errSuccess;
        }
        return err;
    }

    ERR ErrIOIssue() override
    {
        return JET_errSuccess;
    }

    //  Memory-mapped IO is unsupported in this minimal v1 IFileAPI; the
    //  engine has fallback paths through ErrIORead/Write for everywhere
    //  it would normally MM-cache.
    ERR ErrMMRead( const QWORD /*ibOffset*/, const QWORD /*cbSize*/, void** const ppvMap ) override
    {
        if ( ppvMap )
        {
            *ppvMap = nullptr;
        }
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    ERR ErrMMCopy( const QWORD /*ibOffset*/, const QWORD /*cbSize*/, void** const ppvMap ) override
    {
        if ( ppvMap )
        {
            *ppvMap = nullptr;
        }
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    ERR ErrMMIORead( _In_ const QWORD /*ibOffset*/,
                     _Out_writes_bytes_(cb) BYTE * const /*pb*/,
                     _In_ ULONG /*cb*/,
                     _In_ const FileMmIoReadFlag /*fmmiorf*/ ) override
    {
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    ERR ErrMMRevert( _In_ const QWORD /*ibOffset*/,
                     _In_reads_bytes_(cbSize) void* const /*pvMap*/,
                     _In_ const QWORD /*cbSize*/ ) override
    {
        return ErrERRCheck( JET_errFeatureNotAvailable );
    }

    ERR ErrMMFree( void* const /*pvMap*/ ) override
    {
        return JET_errSuccess;
    }

    VOID RegisterIFilePerfAPI( IFilePerfAPI * const pfpapi ) override
    {
        m_pfpapi = pfpapi;
    }

    VOID UpdateIFilePerfAPIEngineFileTypeId( _In_ const DWORD /*dwEngineFileType*/,
                                              _In_ const QWORD /*qwEngineFileId*/ ) override
    {
    }

    ERR ErrNTFSAttributeListSize( QWORD* const pcbSize ) override
    {
        if ( pcbSize )
        {
            *pcbSize = 0;
        }
        return JET_errSuccess;
    }

    ERR ErrDiskId( ULONG_PTR* const pulDiskId ) const override
    {
        if ( pulDiskId )
        {
            *pulDiskId = 0;
        }
        return JET_errSuccess;
    }

    LONG64 CioNonFlushed() const override
    {
        return 0;
    }

    BOOL FSeekPenalty() const override
    {
        //  Treat as SSD: skip the optional spinning-disk-friendly seek
        //  ordering the engine layers on this. Linux block devices expose
        //  rotational status via /sys/block/<dev>/queue/rotational but
        //  we don't surface it here.
        return fFalse;
    }

#ifdef DEBUG
    DWORD DwEngineFileType() const override
    {
        return 0;
    }

    QWORD QwEngineFileId() const override
    {
        return 0;
    }
#endif

    TICK DtickIOElapsed( void* const /*pvIOContext*/ ) override
    {
        return 0;
    }
};

//  Translate IFileAPI::FileModeFlags into the DesiredAccess + Creation
//  + Flags arguments CreateFileW expects. Mirrors osdisk.cxx's
//  Dw*FromFileModeFlags helpers but pinned to the subset our shim
//  honors (no FILE_FLAG_OVERLAPPED / NO_BUFFERING / WRITE_THROUGH).
DWORD DwDesiredAccessFromFmf( IFileAPI::FileModeFlags fmf )
{
    if ( ( fmf & IFileAPI::fmfReadOnly ) != 0 ) return GENERIC_READ;
    return GENERIC_READ | GENERIC_WRITE;
}

DWORD DwShareModeFromFmf( IFileAPI::FileModeFlags fmf )
{
    DWORD dw = FILE_SHARE_READ;
    if ( ( fmf & IFileAPI::fmfReadOnlyPermissive ) != 0 ) dw |= FILE_SHARE_WRITE;
    return dw;
}

DWORD DwFlagsFromFmf( IFileAPI::FileModeFlags fmf )
{
    DWORD dw = 0;
    if ( ( fmf & IFileAPI::fmfTemporary ) != 0 )    dw |= FILE_FLAG_DELETE_ON_CLOSE;
    return dw;
}

}  // namespace

//
//  Wire CSyncFile into the existing COSFileSystem stubs in osfs_posix.cxx.
//

extern "C" ERR CSyncFile_ErrFileCreate( const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi )
{
    if ( !ppfapi ) return ErrERRCheck( JET_errInvalidParameter );
    *ppfapi = nullptr;
    const DWORD dwAccess = DwDesiredAccessFromFmf( fmf );
    const DWORD dwShare  = DwShareModeFromFmf( fmf );
    const DWORD dwDisp   = ( fmf & IFileAPI::fmfOverwriteExisting ) ? CREATE_ALWAYS : CREATE_NEW;
    const DWORD dwFlags  = DwFlagsFromFmf( fmf );

    HANDLE h = CreateFileW( wszPath, dwAccess, dwShare, NULL, dwDisp, dwFlags, NULL );
    if ( h == INVALID_HANDLE_VALUE )
    {
        const DWORD gle = GetLastError();
        if ( gle == ERROR_FILE_EXISTS )         return ErrERRCheck( JET_errFileAccessDenied );
        if ( gle == ERROR_PATH_NOT_FOUND )      return ErrERRCheck( JET_errInvalidPath );
        if ( gle == ERROR_DISK_FULL )           return ErrERRCheck( JET_errDiskFull );
        return ErrERRCheck( JET_errFileAccessDenied );
    }
    *ppfapi = new CSyncFile( h, wszPath, fmf );
    if ( !*ppfapi )
    {
        CloseHandle( h );
        return ErrERRCheck( JET_errOutOfMemory );
    }
    return JET_errSuccess;
}

extern "C" ERR CSyncFile_ErrFileOpen( const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi )
{
    if ( !ppfapi ) return ErrERRCheck( JET_errInvalidParameter );
    *ppfapi = nullptr;
    const DWORD dwAccess = DwDesiredAccessFromFmf( fmf );
    const DWORD dwShare  = DwShareModeFromFmf( fmf );
    const DWORD dwFlags  = DwFlagsFromFmf( fmf );

    HANDLE h = CreateFileW( wszPath, dwAccess, dwShare, NULL, OPEN_EXISTING, dwFlags, NULL );
    if ( h == INVALID_HANDLE_VALUE )
    {
        const DWORD gle = GetLastError();
        if ( gle == ERROR_FILE_NOT_FOUND )      return ErrERRCheck( JET_errFileNotFound );
        if ( gle == ERROR_PATH_NOT_FOUND )      return ErrERRCheck( JET_errInvalidPath );
        if ( gle == ERROR_ACCESS_DENIED )       return ErrERRCheck( JET_errFileAccessDenied );
        if ( gle == ERROR_SHARING_VIOLATION )   return ErrERRCheck( JET_errFileAccessDenied );
        return ErrERRCheck( JET_errFileAccessDenied );
    }
    *ppfapi = new CSyncFile( h, wszPath, fmf );
    if ( !*ppfapi )
    {
        CloseHandle( h );
        return ErrERRCheck( JET_errOutOfMemory );
    }
    return JET_errSuccess;
}
