// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// CIoUringFile — IFileAPI implementation that routes ErrIORead /
// ErrIOWrite / ErrFlushFileBuffers through the process-global io_uring
// (see iouring_posix.cxx). File open / size / metadata operations still
// go through the windows-shim CreateFileW / GetFileSizeEx /
// SetEndOfFile / MoveFileExW path because they aren't on the IO hot
// path and there's no benefit to threading them through io_uring.
//
// Async semantics: when the engine calls ErrIORead/ErrIOWrite with a
// non-null pfnIOComplete, this returns errSuccess immediately and the
// completion thread invokes pfnIOComplete when the CQE arrives. With a
// null pfnIOComplete the call blocks until the IO completes.
//
// What we stub to errFeatureNotAvailable:
//   - Memory-mapped IO (ErrMMRead/Copy/MMIORead/Revert/Free)
//   - NTFS-attribute-list query (no Linux equivalent)
//   - DiskId (always 0)
//   - IOREQ pool (immediate alloc/free)

#include "osstd.hxx"
#include "iouring_posix.hxx"
#include "winapi_kobject.hxx"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace
{

class CIoUringFile : public IFileAPI
{
    HANDLE                  m_hFile;
    int                     m_fd;
    WCHAR                   m_wszPath[ OSFSAPI_MAX_PATH ];
    IFileAPI::FileModeFlags m_fmf;
    IFilePerfAPI*           m_pfpapi;

public:
    CIoUringFile( HANDLE hFile, const WCHAR* wszPath, IFileAPI::FileModeFlags fmf )
        :   m_hFile( hFile ),
            m_fd( -1 ),
            m_fmf( fmf ),
            m_pfpapi( nullptr )
    {
        //  Cache the underlying fd from the windows-shim KObject — the
        //  io_uring submission path needs raw fds, not HANDLEs.
        osposix::KObject* const k = osposix::HandleToK( hFile );
        if ( k && k->kind == osposix::HandleKind::File )
        {
            m_fd = k->fileFd;
        }

        size_t i = 0;
        for ( ; wszPath[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i )
        {
            m_wszPath[ i ] = wszPath[ i ];
        }
        m_wszPath[ i ] = L'\0';
    }

    ~CIoUringFile() override
    {
        if ( m_hFile != INVALID_HANDLE_VALUE )
        {
            CloseHandle( m_hFile );
        }
    }

    HANDLE Handle() const
    {
        return m_hFile;
    }

    //
    //  Identification / properties
    //

    FileModeFlags Fmf() const override
    {
        return m_fmf;
    }

    ERR ErrPath( __out_bcount(cbOSFSAPI_MAX_PATHW) WCHAR* const wszAbsPath ) override
    {
        if ( !wszAbsPath )
        {
            return ErrERRCheck( JET_errInvalidParameter );
        }
        size_t i = 0;
        for ( ; m_wszPath[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i )
        {
            wszAbsPath[ i ] = m_wszPath[ i ];
        }
        wszAbsPath[ i ] = L'\0';
        return JET_errSuccess;
    }

    ERR ErrSize( _Out_ QWORD* const pcbSize, _In_ const FILESIZE filesize ) override
    {
        if ( !pcbSize )
        {
            return ErrERRCheck( JET_errInvalidParameter );
        }
        struct stat st;
        if ( fstat( m_fd, &st ) != 0 )
        {
            return ErrERRCheck( JET_errDiskIO );
        }
        if ( filesize == filesizeOnDisk )
        {
            //  st_blocks reports 512-byte block count regardless of
            //  filesystem block size (POSIX rule).
            *pcbSize = (QWORD)st.st_blocks * 512;
        }
        else
        {
            *pcbSize = (QWORD)st.st_size;
        }
        return JET_errSuccess;
    }

    ERR ErrIsReadOnly( BOOL* const pfReadOnly ) override
    {
        if ( !pfReadOnly )
        {
            return ErrERRCheck( JET_errInvalidParameter );
        }
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
        if ( ftruncate( m_fd, (off_t)cbSize ) != 0 )
        {
            return ErrERRCheck( JET_errDiskIO );
        }
        return JET_errSuccess;
    }

    ERR ErrRename( const WCHAR* const wszAbsPathDest,
                   const BOOL         fOverwriteExisting = fFalse ) override
    {
        const DWORD dwFlags = fOverwriteExisting ? MOVEFILE_REPLACE_EXISTING : 0;
        if ( !MoveFileExW( m_wszPath, wszAbsPathDest, dwFlags ) )
        {
            return ErrERRCheck( JET_errFileAccessDenied );
        }
        size_t i = 0;
        for ( ; wszAbsPathDest[ i ] && i + 1 < OSFSAPI_MAX_PATH; ++i )
        {
            m_wszPath[ i ] = wszAbsPathDest[ i ];
        }
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
        if ( pibStartTrimmedRegion )
        {
            *pibStartTrimmedRegion = 0;
        }
        if ( pcbTrimmed )
        {
            *pcbTrimmed = 0;
        }
        return JET_errSuccess;
    }

    ERR ErrFlushFileBuffers( const IOFLUSHREASON /*iofr*/ ) override
    {
        return osposix::ErrIOUringFsync( m_fd );
    }

    void SetNoFlushNeeded() override
    {
    }

    ERR ErrIOSize( DWORD* const pcbSize ) override
    {
        if ( pcbSize )
        {
            *pcbSize = 4096;
        }
        return JET_errSuccess;
    }

    ERR ErrSectorSize( DWORD* const pcbSize ) override
    {
        if ( pcbSize )
        {
            *pcbSize = 4096;
        }
        return JET_errSuccess;
    }

    ERR ErrReserveIOREQ( const QWORD     /*ibOffset*/,
                         const DWORD     /*cbData*/,
                         const OSFILEQOS /*grbitQOS*/,
                         VOID **         ppioreq ) override
    {
        if ( ppioreq )
        {
            *ppioreq = nullptr;
        }
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
                    const DWORD_PTR     keyIOComplete   = 0,
                    const PfnIOHandoff  pfnIOHandoff    = NULL,
                    const VOID *        /*pioreq*/      = NULL ) override
    {
        //  PfnIOHandoff fires synchronously at submission time — engine
        //  uses it to mark the IO as "in-flight" before this returns.
        if ( pfnIOHandoff )
        {
            FullTraceContext ftcHand;
            ftcHand.etc = tc;
            pfnIOHandoff( JET_errSuccess, this, ftcHand, grbitQOS,
                          ibOffset, cbData, pbData, keyIOComplete, nullptr );
        }

        if ( pfnIOComplete )
        {
            //  Async — heap-allocate IOContext; completion thread frees.
            osposix::IOContext* const ctx = new osposix::IOContext();
            ctx->op             = osposix::IOContext::Op::Read;
            ctx->fileFd         = m_fd;
            ctx->fapi           = this;
            ctx->tc             = tc;
            ctx->qos            = grbitQOS;
            ctx->ibOffset       = ibOffset;
            ctx->cbData         = cbData;
            ctx->pbData         = pbData;
            ctx->keyIOComplete  = keyIOComplete;
            ctx->pfnIOComplete  = pfnIOComplete;
            return osposix::ErrIOUringRead( m_fd, ctx );
        }

        //  Sync — IOContext on stack, completion thread signals cond.
        osposix::IOContext ctx{};
        ctx.op             = osposix::IOContext::Op::Read;
        ctx.fileFd         = m_fd;
        ctx.fapi           = this;
        ctx.tc             = tc;
        ctx.qos            = grbitQOS;
        ctx.ibOffset       = ibOffset;
        ctx.cbData         = cbData;
        ctx.pbData         = pbData;
        ctx.keyIOComplete  = keyIOComplete;
        ctx.pfnIOComplete  = nullptr;
        const ERR err = osposix::ErrIOUringRead( m_fd, &ctx );
        osposix::DestroyContextSyncWait( &ctx );
        return err;
    }

    ERR ErrIOWrite( const TraceContext& tc,
                    const QWORD         ibOffset,
                    const DWORD         cbData,
                    const BYTE* const   pbData,
                    const OSFILEQOS     grbitQOS,
                    const PfnIOComplete pfnIOComplete   = NULL,
                    const DWORD_PTR     keyIOComplete   = 0,
                    const PfnIOHandoff  pfnIOHandoff    = NULL ) override
    {
        if ( pfnIOHandoff )
        {
            FullTraceContext ftcHand;
            ftcHand.etc = tc;
            pfnIOHandoff( JET_errSuccess, this, ftcHand, grbitQOS,
                          ibOffset, cbData, pbData, keyIOComplete, nullptr );
        }

        if ( pfnIOComplete )
        {
            osposix::IOContext* const ctx = new osposix::IOContext();
            ctx->op             = osposix::IOContext::Op::Write;
            ctx->fileFd         = m_fd;
            ctx->fapi           = this;
            ctx->tc             = tc;
            ctx->qos            = grbitQOS;
            ctx->ibOffset       = ibOffset;
            ctx->cbData         = cbData;
            ctx->pbData         = const_cast< BYTE* >( pbData );
            ctx->keyIOComplete  = keyIOComplete;
            ctx->pfnIOComplete  = pfnIOComplete;
            return osposix::ErrIOUringWrite( m_fd, ctx );
        }

        osposix::IOContext ctx{};
        ctx.op             = osposix::IOContext::Op::Write;
        ctx.fileFd         = m_fd;
        ctx.fapi           = this;
        ctx.tc             = tc;
        ctx.qos            = grbitQOS;
        ctx.ibOffset       = ibOffset;
        ctx.cbData         = cbData;
        ctx.pbData         = const_cast< BYTE* >( pbData );
        ctx.keyIOComplete  = keyIOComplete;
        ctx.pfnIOComplete  = nullptr;
        const ERR err = osposix::ErrIOUringWrite( m_fd, &ctx );
        osposix::DestroyContextSyncWait( &ctx );
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
//  + Flags arguments CreateFileW expects.
DWORD DwDesiredAccessFromFmf( IFileAPI::FileModeFlags fmf )
{
    if ( ( fmf & IFileAPI::fmfReadOnly ) != 0 )
    {
        return GENERIC_READ;
    }
    return GENERIC_READ | GENERIC_WRITE;
}

DWORD DwShareModeFromFmf( IFileAPI::FileModeFlags fmf )
{
    DWORD dw = FILE_SHARE_READ;
    if ( ( fmf & IFileAPI::fmfReadOnlyPermissive ) != 0 )
    {
        dw |= FILE_SHARE_WRITE;
    }
    return dw;
}

DWORD DwFlagsFromFmf( IFileAPI::FileModeFlags fmf )
{
    DWORD dw = 0;
    if ( ( fmf & IFileAPI::fmfTemporary ) != 0 )
    {
        dw |= FILE_FLAG_DELETE_ON_CLOSE;
    }
    return dw;
}

}  // namespace

extern "C" ERR CSyncFile_ErrFileCreate( const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi )
{
    if ( !ppfapi )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }
    *ppfapi = nullptr;
    const DWORD dwAccess = DwDesiredAccessFromFmf( fmf );
    const DWORD dwShare  = DwShareModeFromFmf( fmf );
    const DWORD dwDisp   = ( fmf & IFileAPI::fmfOverwriteExisting ) ? CREATE_ALWAYS : CREATE_NEW;
    const DWORD dwFlags  = DwFlagsFromFmf( fmf );

    HANDLE h = CreateFileW( wszPath, dwAccess, dwShare, NULL, dwDisp, dwFlags, NULL );
    if ( h == INVALID_HANDLE_VALUE )
    {
        const DWORD gle = GetLastError();
        if ( gle == ERROR_FILE_EXISTS )
        {
            return ErrERRCheck( JET_errFileAccessDenied );
        }
        if ( gle == ERROR_PATH_NOT_FOUND )
        {
            return ErrERRCheck( JET_errInvalidPath );
        }
        if ( gle == ERROR_DISK_FULL )
        {
            return ErrERRCheck( JET_errDiskFull );
        }
        return ErrERRCheck( JET_errFileAccessDenied );
    }
    *ppfapi = new CIoUringFile( h, wszPath, fmf );
    if ( !*ppfapi )
    {
        CloseHandle( h );
        return ErrERRCheck( JET_errOutOfMemory );
    }
    return JET_errSuccess;
}

extern "C" ERR CSyncFile_ErrFileOpen( const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi )
{
    if ( !ppfapi )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }
    *ppfapi = nullptr;
    const DWORD dwAccess = DwDesiredAccessFromFmf( fmf );
    const DWORD dwShare  = DwShareModeFromFmf( fmf );
    const DWORD dwFlags  = DwFlagsFromFmf( fmf );

    HANDLE h = CreateFileW( wszPath, dwAccess, dwShare, NULL, OPEN_EXISTING, dwFlags, NULL );
    if ( h == INVALID_HANDLE_VALUE )
    {
        const DWORD gle = GetLastError();
        if ( gle == ERROR_FILE_NOT_FOUND )
        {
            return ErrERRCheck( JET_errFileNotFound );
        }
        if ( gle == ERROR_PATH_NOT_FOUND )
        {
            return ErrERRCheck( JET_errInvalidPath );
        }
        if ( gle == ERROR_ACCESS_DENIED )
        {
            return ErrERRCheck( JET_errFileAccessDenied );
        }
        if ( gle == ERROR_SHARING_VIOLATION )
        {
            return ErrERRCheck( JET_errFileAccessDenied );
        }
        return ErrERRCheck( JET_errFileAccessDenied );
    }
    *ppfapi = new CIoUringFile( h, wszPath, fmf );
    if ( !*ppfapi )
    {
        CloseHandle( h );
        return ErrERRCheck( JET_errOutOfMemory );
    }
    return JET_errSuccess;
}
