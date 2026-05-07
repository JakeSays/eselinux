// POSIX equivalent of os/osfs.cxx. The upstream file is ~3800 lines of NTFS-,
// volume-, sector-, and packaged-app-specific logic that has no analogue on
// Linux. The Phase 5 milestone needs link-clean stubs, not a full file system
// implementation — real path/disk/volume work lands in Phase 6 alongside
// io_uring.
//
// What we provide:
//
//   - CDefaultFileSystemConfiguration: verbatim from upstream (pure
//     value-returning members + empty event hooks)
//   - DwShareMode/DwDesiredAccess/DwCreationDispositionFromFileModeFlags:
//     verbatim — they map IFileAPI::FileModeFlags to Win32 constants which
//     exist in the windows-shim and are still the right return type for the
//     COSFile path that uses them
//   - COSFileSystem: stubbed implementation. The constructor matches the
//     upstream signature (so blockcache/_factory.hxx can `new` one); every
//     IFileSystemAPI virtual returns JET_errFeatureNotAvailable
//   - ErrOSFSCreate factories: instantiate a COSFileSystem and hand it back
//   - g_fsconfigDefault, g_fident, g_ctm, g_crep: globals expected by the
//     blockcache factory and OSFSPostterm
//   - Lifecycle entry points (preinit/init/term/postterm) — no-ops that mirror
//     osfs.cxx's behaviour minus the Win8 packaged-app default-path code

#include "osstd.hxx"

#include "_osfs.hxx"

#include "blockcache/_fileidentification.hxx"
#include "blockcache/_cachetelemetry.hxx"
#include "blockcache/_cacherepository.hxx"

#include <unistd.h>     // getcwd


////////////////////////////////////////////////
//  CDefaultFileSystemConfiguration — verbatim from osfs.cxx

CDefaultFileSystemConfiguration::CDefaultFileSystemConfiguration()
    :   m_dtickAccessDeniedRetryPeriod( 100 * 1000 ),
        m_cIOMaxOutstanding( 1024 ),
        m_cIOMaxOutstandingBackground( 32 ),
        m_dtickHungIOThreshhold( 60 * 1024 ),
        m_grbitHungIOActions( JET_bitHungIOEvent ),
        m_cbMaxReadSize( 384 * 1024 ),
        m_cbMaxWriteSize( 384 * 1024 ),
        m_cbMaxReadGapSize( 384 * 1024 ),
        m_permillageSmoothIo( 0 ),
        m_fBlockCacheEnabled( fFalse )
{
}

ULONG CDefaultFileSystemConfiguration::DtickAccessDeniedRetryPeriod()  { return m_dtickAccessDeniedRetryPeriod; }
ULONG CDefaultFileSystemConfiguration::CIOMaxOutstanding()             { return m_cIOMaxOutstanding; }
ULONG CDefaultFileSystemConfiguration::CIOMaxOutstandingBackground()   { return m_cIOMaxOutstandingBackground; }
ULONG CDefaultFileSystemConfiguration::DtickHungIOThreshhold()         { return m_dtickHungIOThreshhold; }
DWORD CDefaultFileSystemConfiguration::GrbitHungIOActions()            { return m_grbitHungIOActions; }
ULONG CDefaultFileSystemConfiguration::CbMaxReadSize()                 { return m_cbMaxReadSize; }
ULONG CDefaultFileSystemConfiguration::CbMaxWriteSize()                { return m_cbMaxWriteSize; }
ULONG CDefaultFileSystemConfiguration::CbMaxReadGapSize()              { return m_cbMaxReadGapSize; }
ULONG CDefaultFileSystemConfiguration::PermillageSmoothIo()            { return m_permillageSmoothIo; }
BOOL  CDefaultFileSystemConfiguration::FBlockCacheEnabled()            { return m_fBlockCacheEnabled; }

ERR CDefaultFileSystemConfiguration::ErrGetBlockCacheConfiguration( _Out_ IBlockCacheConfiguration** const ppbcconfig )
{
    ERR err = JET_errSuccess;
    Alloc( *ppbcconfig = new CDefaultBlockCacheConfiguration() );
HandleError:
    return err;
}

void CDefaultFileSystemConfiguration::EmitEvent(    const EEventType    /* type */,
                                                    const CategoryId    /* catid */,
                                                    const MessageId     /* msgid */,
                                                    const DWORD         /* cString */,
                                                    const WCHAR *       /* rgpwszString */[],
                                                    const LONG          /* lEventLoggingLevel */ )
{
}

void CDefaultFileSystemConfiguration::EmitEvent(    int                 /* haTag */,
                                                    const CategoryId    /* catid */,
                                                    const MessageId     /* msgid */,
                                                    const DWORD         /* cString */,
                                                    const WCHAR *       /* rgpwszString */[],
                                                    int                 /* haCategory */,
                                                    const WCHAR*        /* wszFilename */,
                                                    unsigned _int64     /* qwOffset */,
                                                    DWORD               /* cbSize */ )
{
}

void CDefaultFileSystemConfiguration::EmitFailureTag(   const int           /* haTag */,
                                                        const WCHAR* const  /* wszGuid */,
                                                        const WCHAR* const  /* wszAdditional */ )
{
}

const void* const CDefaultFileSystemConfiguration::PvTraceContext()
{
    return NULL;
}

CDefaultFileSystemConfiguration g_fsconfigDefault;
CFileIdentification g_fident;
CCacheTelemetry g_ctm;
CCacheRepository g_crep( &g_fident, &g_ctm );


////////////////////////////////////////////////
//  FileModeFlags translation — verbatim from osfs.cxx
//
//  The bit values returned here are Win32 CreateFile flags. Nothing on the
//  POSIX side currently consumes them (COSFile's open path is stubbed), but
//  anybody who calls the helpers expects these exact values, so we keep the
//  mapping intact.

DWORD DwDesiredAccessFromFileModeFlags( const IFileAPI::FileModeFlags fmf )
{
    return ( fmf & IFileAPI::fmfReadOnly ) ?
        ( GENERIC_READ ) :
        ( fmf & IFileAPI::fmfReadOnlyClient ) ?
        ( GENERIC_READ | GENERIC_WRITE ) :
        ( fmf & IFileAPI::fmfReadOnlyPermissive ) ?
        ( GENERIC_READ ) :
        ( GENERIC_READ | GENERIC_WRITE | DELETE );
}

DWORD DwShareModeFromFileModeFlags( const IFileAPI::FileModeFlags fmf )
{
    return ( fmf & IFileAPI::fmfReadOnly ) ?
        ( FILE_SHARE_READ ) :
        ( fmf & IFileAPI::fmfReadOnlyClient ) ?
        ( FILE_SHARE_READ ) :
        ( fmf & IFileAPI::fmfReadOnlyPermissive ) ?
        ( FILE_SHARE_READ | FILE_SHARE_WRITE ) :
        ( 0 );
}

DWORD DwCreationDispositionFromFileModeFlags( const BOOL fCreate, const IFileAPI::FileModeFlags fmf )
{
    return fCreate ?
        ( ( fmf & IFileAPI::fmfOverwriteExisting ) ? CREATE_ALWAYS : CREATE_NEW ) :
        ( ( fmf & IFileAPI::fmfOverwriteExisting ) ? TRUNCATE_EXISTING : OPEN_EXISTING );
}


////////////////////////////////////////////////
//  COSFileSystem — stub implementation
//
//  Every IFileSystemAPI virtual returns JET_errFeatureNotAvailable. The class
//  exists so blockcache/_factory.hxx and ErrOSFSCreate can produce an instance
//  for callers that hold an IFileSystemAPI*; real path/file work lands in
//  Phase 6.

COSFileSystem::COSFileSystem( IFileSystemConfiguration * const pfsconfig )
    :   m_pfsconfig( pfsconfig ),
        m_critVolumePathCache( CLockBasicInfo( CSyncBasicInfo( "COSFileSystem::m_critVolumePathCache" ), 0, 0 ) )
{
}

COSFileSystem::~COSFileSystem()
{
}

ERR COSFileSystem::ErrGetLastError( const DWORD /* error */ )
{
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

ERR COSFileSystem::ErrDiskSpace(    const WCHAR* const  /* wszPath */,
                                    QWORD* const        pcbFreeForUser,
                                    QWORD* const        pcbTotalForUser,
                                    QWORD* const        pcbFreeOnDisk )
{
    if ( pcbFreeForUser )  { *pcbFreeForUser = 0; }
    if ( pcbTotalForUser ) { *pcbTotalForUser = 0; }
    if ( pcbFreeOnDisk )   { *pcbFreeOnDisk = 0; }
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

ERR COSFileSystem::ErrFileSectorSize( const WCHAR* const /* wszPath */, DWORD* const pcbSize )
{
    //  Linux exposes the logical block size via ioctl(BLKSSZGET) on
    //  block devices but not generically; engine call sites use the
    //  result for IO alignment and tolerate any reasonable power-of-two
    //  >= 512. 4096 matches the vast majority of modern storage and
    //  matches Win32's GetDiskFreeSpace default sector reporting.
    if ( pcbSize ) { *pcbSize = 4096; }
    return JET_errSuccess;
}

ERR COSFileSystem::ErrFileAtomicWriteSize( const WCHAR* const /* wszPath */, DWORD* const pcbSize )
{
    //  Engine uses this as the log-file sector size and validates that
    //  it's >= 512, a power of two, and that sizeof(LGFILEHDR) % size == 0
    //  (logstream.cxx:218). 4096 satisfies all three on every Linux fs;
    //  POSIX has no portable atomic-write query so we just return that.
    if ( pcbSize ) { *pcbSize = 4096; }
    return JET_errSuccess;
}

ERR COSFileSystem::ErrPathRoot( const WCHAR* const /* wszPath */,
                                __out_bcount(OSFSAPI_MAX_PATH*sizeof(WCHAR)) WCHAR* const wszAbsRootPath )
{
    if ( wszAbsRootPath ) { wszAbsRootPath[0] = L'\0'; }
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

void COSFileSystem::PathVolumeCanonicalAndDiskId(   const WCHAR* const /* wszAbsRootPath */,
                                                    __out_ecount(cchVolumeCanonicalPath) WCHAR* const wszVolumeCanonicalPath,
                                                    _In_ const DWORD cchVolumeCanonicalPath,
                                                    __out_ecount(cchDiskId) WCHAR* const wszDiskId,
                                                    _In_ const DWORD cchDiskId,
                                                    _Out_ DWORD * pdwDiskNumber )
{
    if ( wszVolumeCanonicalPath && cchVolumeCanonicalPath > 0 ) { wszVolumeCanonicalPath[0] = L'\0'; }
    if ( wszDiskId && cchDiskId > 0 )                           { wszDiskId[0] = L'\0'; }
    if ( pdwDiskNumber )                                        { *pdwDiskNumber = 0; }
}

ERR COSFileSystem::ErrPathComplete( _In_z_ const WCHAR* const                           wszPath,
                                    _Out_bytecap_c_(cbOSFSAPI_MAX_PATHW) WCHAR* const   wszAbsPath )
{
    if ( wszAbsPath && wszPath )
    {
        OSStrCbCopyW( wszAbsPath, cbOSFSAPI_MAX_PATHW, wszPath );
    }
    return JET_errSuccess;
}

ERR COSFileSystem::ErrPathParse(    const WCHAR* const                                              wszPath,
                                    __out_bcount(OSFSAPI_MAX_PATH*sizeof(WCHAR)) WCHAR* const       wszFolder,
                                    __out_bcount(OSFSAPI_MAX_PATH*sizeof(WCHAR)) WCHAR* const       wszFileBase,
                                    __out_bcount(OSFSAPI_MAX_PATH*sizeof(WCHAR)) WCHAR* const       wszFileExt )
{
    if ( wszFolder )   { wszFolder[0] = L'\0'; }
    if ( wszFileBase ) { wszFileBase[0] = L'\0'; }
    if ( wszFileExt )  { wszFileExt[0] = L'\0'; }
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

const WCHAR * const COSFileSystem::WszPathFileName( _In_z_ const WCHAR * const wszOptionalFullPath ) const
{
    if ( !wszOptionalFullPath )
    {
        return L"";
    }
    const WCHAR * pwch = wszOptionalFullPath;
    const WCHAR * pwchLast = wszOptionalFullPath;
    while ( *pwch != L'\0' )
    {
        if ( *pwch == L'/' || *pwch == L'\\' )
        {
            pwchLast = pwch + 1;
        }
        ++pwch;
    }
    return pwchLast;
}

ERR COSFileSystem::ErrPathBuild(    __in_z const WCHAR* const                                   wszFolder,
                                    __in_z const WCHAR* const                                   wszFileBase,
                                    __in_z const WCHAR* const                                   wszFileExt,
                                    __out_bcount_z(cbPath) WCHAR* const                         wszPath,
                                    __in_range(cbOSFSAPI_MAX_PATHW, cbOSFSAPI_MAX_PATHW) ULONG  cbPath )
{
    //  Concatenate `<folder>/<base>.<ext>`. Engine call sites pass an
    //  already-normalized folder (trailing '/') from ErrPathFolderNorm,
    //  the file basename, and either the literal extension ".edb" or
    //  ".log". A '.' is inserted between base and ext if ext lacks one.
    ERR err = JET_errSuccess;
    if ( !wszPath || cbPath < sizeof( WCHAR ) )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }
    const ULONG cchMax = cbPath / sizeof( WCHAR );
    ULONG i = 0;

    auto append = [ & ]( const WCHAR* s ) -> ERR {
        if ( !s ) return JET_errSuccess;
        for ( ; *s; ++s )
        {
            if ( i + 1 >= cchMax ) return ErrERRCheck( JET_errOutOfBuffers );
            wszPath[ i++ ] = *s;
        }
        return JET_errSuccess;
    };

    Call( append( wszFolder ) );
    //  ensure trailing separator before appending the basename
    if ( i > 0 && wszPath[ i - 1 ] != L'/' && wszPath[ i - 1 ] != L'\\' )
    {
        if ( i + 1 >= cchMax ) return ErrERRCheck( JET_errOutOfBuffers );
        wszPath[ i++ ] = L'/';
    }
    Call( append( wszFileBase ) );
    if ( wszFileExt && wszFileExt[ 0 ] && wszFileExt[ 0 ] != L'.' )
    {
        if ( i + 1 >= cchMax ) return ErrERRCheck( JET_errOutOfBuffers );
        wszPath[ i++ ] = L'.';
    }
    Call( append( wszFileExt ) );
    wszPath[ i ] = L'\0';
    return JET_errSuccess;
HandleError:
    if ( cbPath >= sizeof( WCHAR ) ) wszPath[ 0 ] = L'\0';
    return err;
}

ERR COSFileSystem::ErrPathFolderNorm( __inout_bcount(cbSize) PWSTR const wszFolder, DWORD cbSize )
{
    //  Engine call sites pass paths like ".\\" / "edb" / etc. and expect a
    //  normalized absolute folder ending in a path separator. Real Win32
    //  ErrPathFolderNorm calls GetFullPathName + appends trailing '\\'; we
    //  do the POSIX equivalent — turn relative paths into absolute via
    //  cwd, convert backslashes to forward slashes, and ensure a trailing
    //  '/'. We don't follow symlinks (no realpath) because the path may
    //  point to a not-yet-created directory.
    if ( !wszFolder || cbSize < 2 * sizeof( WCHAR ) )
    {
        return ErrERRCheck( JET_errInvalidParameter );
    }

    const size_t cchMax = cbSize / sizeof( WCHAR );

    //  Convert backslashes to forward slashes in place.
    for ( WCHAR* p = wszFolder; *p; ++p )
    {
        if ( *p == L'\\' ) *p = L'/';
    }

    //  Compute absolute path. If wszFolder isn't absolute (doesn't start
    //  with '/'), prepend cwd.
    WCHAR wszTmp[ 4096 ];
    size_t cchTmp = 0;
    if ( wszFolder[ 0 ] != L'/' )
    {
        char cwdBuf[ 4096 ];
        if ( !getcwd( cwdBuf, sizeof( cwdBuf ) ) )
        {
            return ErrERRCheck( JET_errOutOfBuffers );
        }
        for ( size_t i = 0; cwdBuf[ i ] && cchTmp + 1 < _countof( wszTmp ); ++i )
        {
            wszTmp[ cchTmp++ ] = (WCHAR)(unsigned char)cwdBuf[ i ];
        }
        if ( cchTmp == 0 || wszTmp[ cchTmp - 1 ] != L'/' )
        {
            if ( cchTmp + 1 >= _countof( wszTmp ) ) return ErrERRCheck( JET_errOutOfBuffers );
            wszTmp[ cchTmp++ ] = L'/';
        }
        //  Strip a leading "./" from wszFolder before appending — we already
        //  have the absolute prefix.
        const WCHAR* tail = wszFolder;
        while ( tail[ 0 ] == L'.' && tail[ 1 ] == L'/' ) tail += 2;
        for ( ; *tail; ++tail )
        {
            if ( cchTmp + 1 >= _countof( wszTmp ) ) return ErrERRCheck( JET_errOutOfBuffers );
            wszTmp[ cchTmp++ ] = *tail;
        }
    }
    else
    {
        for ( WCHAR* p = wszFolder; *p; ++p )
        {
            if ( cchTmp + 1 >= _countof( wszTmp ) ) return ErrERRCheck( JET_errOutOfBuffers );
            wszTmp[ cchTmp++ ] = *p;
        }
    }

    //  Ensure trailing '/'.
    if ( cchTmp == 0 || wszTmp[ cchTmp - 1 ] != L'/' )
    {
        if ( cchTmp + 1 >= _countof( wszTmp ) ) return ErrERRCheck( JET_errOutOfBuffers );
        wszTmp[ cchTmp++ ] = L'/';
    }
    wszTmp[ cchTmp ] = L'\0';

    if ( cchTmp + 1 > cchMax )
    {
        return ErrERRCheck( JET_errOutOfBuffers );
    }
    for ( size_t i = 0; i <= cchTmp; ++i )
    {
        wszFolder[ i ] = wszTmp[ i ];
    }
    return JET_errSuccess;
}

BOOL COSFileSystem::FPathIsRelative( _In_ PCWSTR wszPath )
{
    return ( wszPath && wszPath[0] != L'/' );
}

ERR COSFileSystem::ErrPathExists( _In_ PCWSTR /* wszPath */, _Out_opt_ BOOL* pfIsDirectory )
{
    if ( pfIsDirectory ) { *pfIsDirectory = fFalse; }
    return ErrERRCheck( JET_errFileNotFound );
}

ERR COSFileSystem::ErrPathFolderDefault(    _Out_z_bytecap_(cbSize) PWSTR const wszFolder,
                                            _In_ DWORD                          cbSize,
                                            _Out_ BOOL *                        pfCanProcessUseRelativePaths )
{
    if ( wszFolder && cbSize >= 2 * sizeof(WCHAR) )
    {
        wszFolder[0] = L'.';
        wszFolder[1] = L'/';
        wszFolder[2] = L'\0';
    }
    if ( pfCanProcessUseRelativePaths ) { *pfCanProcessUseRelativePaths = fTrue; }
    return JET_errSuccess;
}

ERR COSFileSystem::ErrGetTempFolder( _Out_z_cap_(cchFolder) PWSTR const wszFolder, _In_ const DWORD cchFolder )
{
    if ( wszFolder && cchFolder >= 5 )
    {
        wszFolder[0] = L'/'; wszFolder[1] = L't'; wszFolder[2] = L'm'; wszFolder[3] = L'p'; wszFolder[4] = L'\0';
    }
    return JET_errSuccess;
}

ERR COSFileSystem::ErrGetTempFileName( _In_z_ PWSTR const                          /* wszFolder */,
                                       _In_z_ PWSTR const                          /* wszPrefix */,
                                       _Out_z_cap_(OSFSAPI_MAX_PATH) PWSTR const   wszFileName )
{
    if ( wszFileName ) { wszFileName[0] = L'\0'; }
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

ERR COSFileSystem::ErrFolderCreate( const WCHAR* const /* wszPath */ )    { return ErrERRCheck( JET_errFeatureNotAvailable ); }
ERR COSFileSystem::ErrFolderRemove( const WCHAR* const /* wszPath */ )    { return ErrERRCheck( JET_errFeatureNotAvailable ); }

namespace
{
    //  Minimal IFileFindAPI: an empty iterator. ErrNext returns
    //  errFileNotFound on first call so engine consumers (e.g.,
    //  ErrLGGetGenerationRangeExt) treat the directory as empty
    //  and proceed with fresh-log creation. A real FindFirstFileW-
    //  driven implementation belongs alongside the io_uring file
    //  layer; this stub is sufficient for first-time database open.
    class CEmptyFileFind : public IFileFindAPI
    {
    public:
        ~CEmptyFileFind() override {}
        ERR ErrNext() override { return ErrERRCheck( JET_errFileNotFound ); }
        ERR ErrIsFolder( BOOL* const pfFolder ) override
        { if ( pfFolder ) *pfFolder = fFalse; return ErrERRCheck( JET_errFileNotFound ); }
        ERR ErrPath( __out_bcount(OSFSAPI_MAX_PATH*sizeof(WCHAR)) WCHAR* const wszAbsFoundPath ) override
        { if ( wszAbsFoundPath ) wszAbsFoundPath[0] = L'\0'; return ErrERRCheck( JET_errFileNotFound ); }
        ERR ErrSize( _Out_ QWORD* const pcbSize, _In_ const IFileAPI::FILESIZE /*file*/ ) override
        { if ( pcbSize ) *pcbSize = 0; return ErrERRCheck( JET_errFileNotFound ); }
        ERR ErrIsReadOnly( BOOL* const pfReadOnly ) override
        { if ( pfReadOnly ) *pfReadOnly = fFalse; return ErrERRCheck( JET_errFileNotFound ); }
    };
}

ERR COSFileSystem::ErrFileFind( const WCHAR* const /* wszFind */, IFileFindAPI** const ppffapi )
{
    if ( !ppffapi ) return ErrERRCheck( JET_errInvalidParameter );
    *ppffapi = new CEmptyFileFind();
    if ( !*ppffapi ) return ErrERRCheck( JET_errOutOfMemory );
    return JET_errSuccess;
}

ERR COSFileSystem::ErrFileDelete( const WCHAR* const /* wszPath */ )      { return ErrERRCheck( JET_errFeatureNotAvailable ); }
ERR COSFileSystem::ErrFileMove(   const WCHAR* const /* wszPathSource */,
                                  const WCHAR* const /* wszPathDest */,
                                  const BOOL         /* fOverwriteExisting */ )
{
    return ErrERRCheck( JET_errFeatureNotAvailable );
}
ERR COSFileSystem::ErrFileCopy(   const WCHAR* const /* wszPathSource */,
                                  const WCHAR* const /* wszPathDest */,
                                  const BOOL         /* fOverwriteExisting */ )
{
    return ErrERRCheck( JET_errFeatureNotAvailable );
}

extern "C" ERR CSyncFile_ErrFileCreate( const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi );
extern "C" ERR CSyncFile_ErrFileOpen(   const WCHAR* wszPath, IFileAPI::FileModeFlags fmf, IFileAPI** ppfapi );

ERR COSFileSystem::ErrFileCreate(   _In_z_ const WCHAR* const       wszPath,
                                    _In_   IFileAPI::FileModeFlags  fmf,
                                    _Out_  IFileAPI** const         ppfapi )
{
    return CSyncFile_ErrFileCreate( wszPath, fmf, ppfapi );
}

ERR COSFileSystem::ErrFileOpen( _In_z_ const WCHAR* const       wszPath,
                                _In_   IFileAPI::FileModeFlags  fmf,
                                _Out_  IFileAPI** const         ppfapi )
{
    return CSyncFile_ErrFileOpen( wszPath, fmf, ppfapi );
}


////////////////////////////////////////////////
//  ErrOSFSCreate — verbatim from upstream, minus the block-cache wrap

ERR ErrOSFSCreate( _Out_ IFileSystemAPI** const ppfsapi )
{
    return ErrOSFSCreate( NULL, ppfsapi );
}

ERR ErrOSFSCreate(  _In_ IFileSystemConfiguration * const   pfsconfig,
                    _Out_ IFileSystemAPI** const            ppfsapi )
{
    ERR                             err         = JET_errSuccess;
    IFileSystemConfiguration* const pfsconfigT  = pfsconfig ? pfsconfig : &g_fsconfigDefault;
    IFileSystemAPI*                 pfsapi      = NULL;

    Alloc( pfsapi = new COSFileSystem( pfsconfigT ) );

    *ppfsapi = pfsapi;
    pfsapi = NULL;

HandleError:
    delete pfsapi;
    return err;
}


////////////////////////////////////////////////
//  Lifecycle
//
//  Upstream OSFSPostterm tears down g_fident/g_crep and frees the cached
//  default-path string. We keep the object cleanups but skip the path string —
//  ErrOSFSInit no longer allocates one (no Win8 packaged-app code on Linux).

void OSFSPostterm()
{
    g_fident.Cleanup();
    g_crep.Cleanup();
}

BOOL FOSFSPreinit()
{
    return fTrue;
}

ERR ErrOSFSInit()
{
    return JET_errSuccess;
}

VOID OSFSTerm()
{
}
