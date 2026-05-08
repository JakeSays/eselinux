// POSIX implementation of os/memory.cxx. Replaces the Win32 surface
// (HeapAlloc / VirtualAlloc / GlobalMemoryStatusEx / NtQueryInformationProcess)
// with portable equivalents (malloc / mmap / sysconf / /proc/self/*).
//
// Tracking-bookkeeping (the MEM_CHECK and OSMemoryIDumpAlloc machinery) is
// stubbed because it is debug-only and the Linux port doesn't carry it.

#include "osstd.hxx"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <unistd.h>


//  Globals expected by the engine.

INT g_fMemCheck = fFalse;

DWORD g_cAllocHeap = 0;
DWORD g_cFreeHeap = 0;
DWORD g_cbAllocHeap = 0;
DWORD_PTR g_cbReservePage = 0;
DWORD_PTR g_cbCommitPage = 0;


//  System Memory Attributes — populated by FOSMemoryPreinit.

LOCAL DWORD g_dwPageReserveGran = 0;
LOCAL DWORD g_dwPageCommitGran = 0;
LOCAL QWORD g_cbMemoryTotal = 0;
LOCAL DWORD_PTR g_cbPageReserveTotal = 0;


DWORD OSMemoryPageReserveGranularity()
{
    return g_dwPageReserveGran;
}

DWORD OSMemoryPageCommitGranularity()
{
    return g_dwPageCommitGran;
}

QWORD OSMemoryAvailable()
{
    struct sysinfo si = {};
    if ( sysinfo( &si ) == 0 )
    {
        return (QWORD)si.freeram * (QWORD)si.mem_unit;
    }
    return 0;
}

QWORD OSMemoryTotal()
{
    return g_cbMemoryTotal;
}

DWORD_PTR OSMemoryPageReserveAvailable()
{
    //  On Linux the per-process VA space is effectively unbounded compared
    //  to physical memory; report the smaller of the two as a usable proxy.
    const QWORD cbAvailPhys = OSMemoryAvailable();
    return (DWORD_PTR)min( cbAvailPhys, (QWORD)g_cbPageReserveTotal );
}

DWORD_PTR OSMemoryPageReserveTotal()
{
    return g_cbPageReserveTotal;
}

DWORD_PTR OSMemoryPageWorkingSetPeak()
{
    //  /proc/self/status, VmHWM line (kB).
    FILE* fp = fopen( "/proc/self/status", "r" );
    if ( !fp ) return 0;
    char line[256];
    DWORD_PTR cb = 0;
    while ( fgets( line, sizeof( line ), fp ) )
    {
        if ( strncmp( line, "VmHWM:", 6 ) == 0 )
        {
            unsigned long kb = 0;
            sscanf( line + 6, "%lu", &kb );
            cb = (DWORD_PTR)kb * 1024;
            break;
        }
    }
    fclose( fp );
    return cb;
}

DWORD OSMemoryPageEvictionCount()
{
    //  Linux exposes pgmajfault in /proc/self/stat field 12 as a per-process
    //  proxy for hard faults; engine only needs a monotonic-ish number.
    FILE* fp = fopen( "/proc/self/stat", "r" );
    if ( !fp ) return 0;
    char buf[1024];
    size_t n = fread( buf, 1, sizeof( buf ) - 1, fp );
    fclose( fp );
    buf[n] = 0;

    //  Skip past comm field which can contain spaces and parens.
    char* p = strrchr( buf, ')' );
    if ( !p ) return 0;
    p++;
    DWORD val = 0;
    int field = 1;
    char* tok = strtok( p, " " );
    //  After the close paren we are at field 3 (state). Field 12 is majflt.
    while ( tok && field < 10 )
    {
        tok = strtok( nullptr, " " );
        field++;
    }
    if ( tok ) val = (DWORD)strtoul( tok, nullptr, 10 );
    return val;
}

void OSMemoryGetProcessMemStats( MEMSTAT * const pmemstat )
{
    memset( pmemstat, 0, sizeof( *pmemstat ) );
    FILE* fp = fopen( "/proc/self/status", "r" );
    if ( !fp ) return;
    char line[256];
    while ( fgets( line, sizeof( line ), fp ) )
    {
        unsigned long kb = 0;
        if ( strncmp( line, "VmRSS:", 6 ) == 0 )
        {
            sscanf( line + 6, "%lu", &kb );
            pmemstat->cbWorkingSetSize = (SIZE_T)kb * 1024;
        }
        else if ( strncmp( line, "VmHWM:", 6 ) == 0 )
        {
            sscanf( line + 6, "%lu", &kb );
            pmemstat->cbPeakWorkingSetSize = (SIZE_T)kb * 1024;
        }
        else if ( strncmp( line, "VmSwap:", 7 ) == 0 )
        {
            sscanf( line + 7, "%lu", &kb );
            pmemstat->cbPagefileUsage = (SIZE_T)kb * 1024;
            pmemstat->cbPeakPagefileUsage = pmemstat->cbPagefileUsage;
        }
        else if ( strncmp( line, "VmData:", 7 ) == 0 )
        {
            sscanf( line + 7, "%lu", &kb );
            pmemstat->cbPrivateUsage = (SIZE_T)kb * 1024;
        }
    }
    fclose( fp );

    struct rusage ru = {};
    if ( getrusage( RUSAGE_SELF, &ru ) == 0 )
    {
        pmemstat->cPageFaultCount = (DWORD)( ru.ru_majflt + ru.ru_minflt );
    }
}

DWORD_PTR OSMemoryQuotaTotal()
{
    struct rlimit rl = {};
    if ( getrlimit( RLIMIT_AS, &rl ) == 0 && rl.rlim_cur != RLIM_INFINITY )
    {
        return (DWORD_PTR)rl.rlim_cur;
    }
    return (DWORD_PTR)g_cbMemoryTotal;
}


//  Heap allocation

void* PvOSMemoryHeapAlloc__( const size_t cbSize )
{
    void* pv = malloc( cbSize ? cbSize : 1 );
    return pv;
}

void* PvOSMemoryHeapAllocAlign__( const size_t cbSize, const size_t cbAlign )
{
    //  Match Win32 _aligned_malloc semantics: a 0-byte request returns a
    //  valid (uniquely-addressable) pointer rather than NULL. The engine's
    //  CLookaside::ErrInit relies on this — m_cItems can be 0 for resource
    //  managers without lookaside entries, and a NULL return there is
    //  treated as JET_errOutOfMemory.
    const size_t cbActual = cbSize ? cbSize : 1;
    //  posix_memalign requires a power-of-two alignment that is a multiple
    //  of sizeof(void*).
    size_t cbAlignReal = cbAlign < sizeof( void* ) ? sizeof( void* ) : cbAlign;
    void* pv = nullptr;
    if ( posix_memalign( &pv, cbAlignReal, cbActual ) != 0 )
    {
        return nullptr;
    }
    return pv;
}

void OSMemoryHeapFree( void* const pv )
{
    free( pv );
}

void OSMemoryHeapFreeAlign( void* const pv )
{
    free( pv );
}

void OSMemorySecureZero( PVOID pv, SIZE_T cnt )
{
    if ( !pv || !cnt ) return;
    explicit_bzero( pv, cnt );
}


//  Zero-detection helpers — verbatim ports of the upstream memory.cxx
//  routines. Pure portable C++; live here because we don't compile the
//  Win32-flavored memory.cxx on Linux. Used by blockcache validators and
//  log/scrub fast paths.

BOOL FUtilZeroed( __in_bcount(cbData) const BYTE * pbData, _In_ const size_t cbData )
{
    Assert( pbData != NULL );

    if ( cbData == 0 )
    {
        return fTrue;
    }

    const size_t cbAlignment = sizeof( QWORD );
    const BYTE* const pbDataMax = pbData + cbData - 1;
    const BYTE* const pbDataAlignedFirst = (BYTE*)roundup( (DWORD_PTR)pbData, cbAlignment );
    const BYTE* const pbDataPostAligned  = (BYTE*)rounddn( (DWORD_PTR)pbDataMax, cbAlignment );

    while ( ( pbData <= pbDataMax ) && ( pbData < pbDataAlignedFirst ) )
    {
        if ( *pbData != 0 ) return false;
        pbData++;
    }
    while ( pbData < pbDataPostAligned )
    {
        if ( *( (QWORD*)pbData ) != 0 ) return false;
        pbData += cbAlignment;
    }
    while ( pbData <= pbDataMax )
    {
        if ( *pbData != 0 ) return false;
        pbData++;
    }
    return fTrue;
}

size_t IbUtilLastNonZeroed( __in_bcount(cbData) const BYTE * pbData, _In_ const size_t cbData )
{
    Assert( pbData != NULL );
    if ( cbData == 0 ) return 0;

    size_t ibData = cbData;
    do
    {
        ibData--;
        if ( pbData[ ibData ] != 0 ) return ibData;
    }
    while ( ibData > 0 );

    return cbData;
}


//  We track the size of every reserve/alloc so OSMemoryPageFree() can release it.
//  Linux munmap() requires the original length, so we stash it in a thin map.
//  A simpler scheme: prepend the size to the allocation. But we need the
//  caller's pointer to be the mmap'd base (engine assumes it). Use a side map.
//
//  For practical purposes we keep it small: a static array protected by a
//  pthread mutex. The engine only reserves a handful of large regions.

#include <pthread.h>

namespace {

struct PageRegion
{
    void*  pv;
    size_t cb;
};

constexpr size_t cMaxPageRegions = 4096;
PageRegion g_rgRegion[ cMaxPageRegions ];
pthread_mutex_t g_mutexRegions = PTHREAD_MUTEX_INITIALIZER;

void RegionRecord( void* pv, size_t cb )
{
    pthread_mutex_lock( &g_mutexRegions );
    for ( size_t i = 0; i < cMaxPageRegions; ++i )
    {
        if ( !g_rgRegion[ i ].pv )
        {
            g_rgRegion[ i ].pv = pv;
            g_rgRegion[ i ].cb = cb;
            break;
        }
    }
    pthread_mutex_unlock( &g_mutexRegions );
}

size_t RegionLookupAndRemove( void* pv )
{
    size_t cb = 0;
    pthread_mutex_lock( &g_mutexRegions );
    for ( size_t i = 0; i < cMaxPageRegions; ++i )
    {
        if ( g_rgRegion[ i ].pv == pv )
        {
            cb = g_rgRegion[ i ].cb;
            g_rgRegion[ i ].pv = nullptr;
            g_rgRegion[ i ].cb = 0;
            break;
        }
    }
    pthread_mutex_unlock( &g_mutexRegions );
    return cb;
}

size_t RegionLookup( const void* pv )
{
    size_t cb = 0;
    pthread_mutex_lock( &g_mutexRegions );
    for ( size_t i = 0; i < cMaxPageRegions; ++i )
    {
        if ( g_rgRegion[ i ].pv == pv )
        {
            cb = g_rgRegion[ i ].cb;
            break;
        }
    }
    pthread_mutex_unlock( &g_mutexRegions );
    return cb;
}

} // namespace


//  Page allocation — backed by mmap. Returns memory aligned to the engine's
//  reserve granularity (g_dwPageReserveGran, 64K on this build) — *not* just
//  to the OS page size. Win32 VirtualAlloc gives 64K-aligned allocations
//  natively; mmap only guarantees page (4K) alignment, so we over-allocate
//  and trim. The engine's resource manager (cresmgr.cxx) masks low bits off
//  pointers to find the section header (`pv & maskSection`), so a sub-64K-
//  aligned chunk would point that mask into unrelated memory and SIGSEGV
//  on free.

namespace {

void* PvAlignedMmap( const size_t cbSize, void* const pvHint, const int prot, const int flags )
{
    const size_t cbAlign = (size_t)g_dwPageReserveGran;
    Assert( cbAlign && ( ( cbAlign & ( cbAlign - 1 ) ) == 0 ) );

    //  If the OS page granularity already meets the engine's reserve
    //  granularity, fast-path: a single mmap of cbSize.
    const size_t cbPage = (size_t)g_dwPageCommitGran;
    if ( cbAlign <= cbPage )
    {
        void* pv = mmap( pvHint, cbSize, prot, flags, -1, 0 );
        return ( pv == MAP_FAILED ) ? nullptr : pv;
    }

    //  Over-allocate by (alignment - page) so that within the mapping there
    //  is at least one cbAlign-aligned address with cbSize of room after it.
    const size_t cbExtra = cbAlign - cbPage;
    const size_t cbTotal = cbSize + cbExtra;
    void* base = mmap( pvHint, cbTotal, prot, flags, -1, 0 );
    if ( base == MAP_FAILED )
    {
        return nullptr;
    }

    const uintptr_t uBase    = (uintptr_t)base;
    const uintptr_t uAligned = ( uBase + cbAlign - 1 ) & ~(uintptr_t)( cbAlign - 1 );
    const size_t cbPrefix    = (size_t)( uAligned - uBase );
    const size_t cbSuffix    = cbExtra - cbPrefix;

    if ( cbPrefix )
    {
        munmap( base, cbPrefix );
    }
    if ( cbSuffix )
    {
        munmap( (char*)uAligned + cbSize, cbSuffix );
    }
    return (void*)uAligned;
}

}  // namespace

void* PvOSMemoryPageAlloc__( const size_t cbSize, void* const pvHint, const BOOL /*fAllocTopDown*/ )
{
    if ( cbSize == 0 ) return nullptr;
    void* pv = PvAlignedMmap( cbSize, pvHint, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS );
    if ( pv ) RegionRecord( pv, cbSize );
    return pv;
}

void* PvOSMemoryPageReserve__( const size_t cbSize, void* const pvHint )
{
    if ( cbSize == 0 ) return nullptr;
    void* pv = PvAlignedMmap( cbSize, pvHint, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE );
    if ( pv ) RegionRecord( pv, cbSize );
    return pv;
}

void OSMemoryPageFree( void* const pv )
{
    if ( !pv ) return;
    size_t cb = RegionLookupAndRemove( pv );
    if ( cb ) munmap( pv, cb );
}

//  Win32 VirtualAlloc(MEM_COMMIT) / VirtualProtect / VirtualFree(MEM_DECOMMIT)
//  silently round the address DOWN to the OS page boundary and the size UP
//  so a sub-page request flips the entire containing page. Linux mprotect /
//  madvise reject unaligned addresses with EINVAL — replicate the Win32
//  rounding so the engine's call sites work without per-call adjustments.
namespace {

void PageAlignRange( const void* pv, size_t cb, void** ppvOut, size_t* pcbOut )
{
    const size_t      cbPage      = (size_t)g_dwPageCommitGran;
    const uintptr_t   uPv         = (uintptr_t)pv;
    const uintptr_t   uPvAligned  = uPv & ~(uintptr_t)( cbPage - 1 );
    const uintptr_t   uPvEnd      = ( uPv + cb + cbPage - 1 ) & ~(uintptr_t)( cbPage - 1 );
    *ppvOut = (void*)uPvAligned;
    *pcbOut = (size_t)( uPvEnd - uPvAligned );
}

}  // namespace

BOOL FOSMemoryPageCommit( void* const pv, const size_t cb )
{
    if ( !pv || !cb ) return fFalse;
    void* pvAligned; size_t cbAligned;
    PageAlignRange( pv, cb, &pvAligned, &cbAligned );
    return mprotect( pvAligned, cbAligned, PROT_READ | PROT_WRITE ) == 0 ? fTrue : fFalse;
}

void OSMemoryPageDecommit( void* const pv, const size_t cb )
{
    if ( !pv || !cb ) return;
    void* pvAligned; size_t cbAligned;
    PageAlignRange( pv, cb, &pvAligned, &cbAligned );
    //  Drop physical pages but keep the reservation; mark unreadable so
    //  callers that incorrectly touch decommitted memory fault.
    madvise( pvAligned, cbAligned, MADV_DONTNEED );
    mprotect( pvAligned, cbAligned, PROT_NONE );
}

void OSMemoryPageReset( void* const pv, const size_t cbSize, const BOOL fToss )
{
    if ( !pv || !cbSize ) return;
    void* pvAligned; size_t cbAligned;
    PageAlignRange( pv, cbSize, &pvAligned, &cbAligned );
    madvise( pvAligned, cbAligned, fToss ? MADV_DONTNEED : MADV_FREE );
}

void OSMemoryPageProtect( void* const pv, const size_t cbSize )
{
    if ( !pv || !cbSize ) return;
    void* pvAligned; size_t cbAligned;
    PageAlignRange( pv, cbSize, &pvAligned, &cbAligned );
    mprotect( pvAligned, cbAligned, PROT_READ );
}

void OSMemoryPageUnprotect( void* const pv, const size_t cbSize )
{
    if ( !pv || !cbSize ) return;
    void* pvAligned; size_t cbAligned;
    PageAlignRange( pv, cbSize, &pvAligned, &cbAligned );
    mprotect( pvAligned, cbAligned, PROT_READ | PROT_WRITE );
}

BOOL FOSMemoryPageLock( void* const pv, const size_t cb )
{
    if ( !pv || !cb ) return fFalse;
    return mlock( pv, cb ) == 0 ? fTrue : fFalse;
}

void OSMemoryPageUnlock( void* const pv, const size_t cb )
{
    if ( !pv || !cb ) return;
    munlock( pv, cb );
}

BOOL FOSMemoryPageResident( void* const pv, const size_t cb )
{
    if ( !pv || !cb ) return fFalse;
    const size_t cbPage = sysconf( _SC_PAGESIZE );
    const size_t cPage = ( cb + cbPage - 1 ) / cbPage;
    unsigned char rgvec[ 1024 ];
    if ( cPage > sizeof( rgvec ) ) return fFalse;
    if ( mincore( pv, cb, rgvec ) != 0 ) return fFalse;
    for ( size_t i = 0; i < cPage; ++i )
    {
        if ( !( rgvec[ i ] & 1 ) ) return fFalse;
    }
    return fTrue;
}

BOOL FOSMemoryPageAllocated( const void * const pv, const size_t /*cb*/ )
{
    return RegionLookup( pv ) != 0 ? fTrue : fFalse;
}

BOOL FOSMemoryFileMapped( const void * const /*pv*/, const size_t /*cb*/ )
{
    //  Engine uses this only in unit tests / asserts. Conservative answer:
    //  we don't currently file-map memory in the POSIX layer.
    return fFalse;
}

BOOL FOSMemoryFileMappedCowed( const void * const /*pv*/, const size_t /*cb*/ )
{
    return fFalse;
}


//  Override hooks — accepted but ignored on Linux (no NT internals to swap).

const void* PvOSMemoryHookNtQueryInformationProcess( const void* const /*pfnNew*/ )
{
    return nullptr;
}

const void* PvOSMemoryHookNtQuerySystemInformation( const void* const /*pfnNew*/ )
{
    return nullptr;
}

const void* PvOSMemoryHookGlobalMemoryStatus( const void* const /*pfnNew*/ )
{
    return nullptr;
}


//  Tracking helpers — debug-only on Windows, no-ops here.

void OSMemoryIInsertHeapAlloc( void* const /*pv*/, const size_t /*cb*/, const CHAR* /*szFile*/, LONG /*lLine*/ ) {}
void OSMemoryIDeleteHeapAlloc( void* const /*pv*/, size_t /*cb*/ ) {}
void OSMemoryIInsertPageAlloc( void* const /*pv*/, const size_t /*cb*/, const CHAR* /*szFile*/, LONG /*lLine*/ ) {}
void OSMemoryIDeletePageAlloc( void* /*pv*/, const size_t /*cb*/ ) {}

BOOL FOSMemoryNewMemCheck_( __in_z const CHAR* const /*szFileName*/, const ULONG /*ulLine*/ )
{
    return fTrue;
}


//  Bitmap impls — logic isn't OS-specific; we replicate the reference impl.

CFixedBitmap::CFixedBitmap( _Out_writes_bytes_( cbBuffer ) void * pbBuffer, _In_ ULONG cbBuffer )
    : m_cbit( (size_t)cbBuffer * 8 ),
      m_rgbit( pbBuffer )
{
    memset( pbBuffer, 0, cbBuffer );
}

CFixedBitmap::~CFixedBitmap() {}

IBitmapAPI::ERR CFixedBitmap::ErrInitBitmap( _In_ const size_t cbit )
{
    if ( cbit > m_cbit ) return IBitmapAPI::ERR::errInvalidParameter;
    m_cbit = cbit;
    memset( m_rgbit, 0, ( cbit + 7 ) / 8 );
    return IBitmapAPI::ERR::errSuccess;
}

IBitmapAPI::ERR CFixedBitmap::ErrSet( _In_ const size_t iBit, _In_ const BOOL fValue )
{
    if ( iBit >= m_cbit ) return IBitmapAPI::ERR::errInvalidParameter;
    BYTE* pb = (BYTE*)m_rgbit;
    const BYTE mask = (BYTE)( 1u << ( iBit & 7 ) );
    if ( fValue )   pb[ iBit >> 3 ] = (BYTE)( pb[ iBit >> 3 ] | mask );
    else            pb[ iBit >> 3 ] = (BYTE)( pb[ iBit >> 3 ] & ~mask );
    return IBitmapAPI::ERR::errSuccess;
}

IBitmapAPI::ERR CFixedBitmap::ErrGet( _In_ const size_t iBit, _Out_ BOOL* const pfValue )
{
    if ( iBit >= m_cbit ) return IBitmapAPI::ERR::errInvalidParameter;
    const BYTE* pb = (const BYTE*)m_rgbit;
    *pfValue = ( pb[ iBit >> 3 ] & ( 1u << ( iBit & 7 ) ) ) ? fTrue : fFalse;
    return IBitmapAPI::ERR::errSuccess;
}


CSparseBitmap::CSparseBitmap()
    : m_cbit( 0 ),
      m_rgbit( nullptr ),
      m_cbitUpdate( 0 ),
      m_cbitCommit( 0 ),
      m_rgbitCommit( nullptr ),
      m_shfCommit( 0 )
{}

CSparseBitmap::~CSparseBitmap()
{
    if ( m_rgbit ) OSMemoryHeapFree( m_rgbit );
    m_rgbit = nullptr;
    m_cbit = 0;
    m_cbitUpdate = 0;
}

IBitmapAPI::ERR CSparseBitmap::ErrInitBitmap( const size_t cbit )
{
    if ( m_rgbit ) OSMemoryHeapFree( m_rgbit );
    m_cbit = cbit;
    m_cbitUpdate = cbit;
    const size_t cb = ( cbit + 7 ) / 8;
    m_rgbit = cb ? PvOSMemoryHeapAlloc( cb ) : nullptr;
    if ( cb && !m_rgbit ) return IBitmapAPI::ERR::errOutOfMemory;
    if ( m_rgbit ) memset( m_rgbit, 0, cb );
    return IBitmapAPI::ERR::errSuccess;
}

IBitmapAPI::ERR CSparseBitmap::ErrReset( const size_t cbit )
{
    return ErrInitBitmap( cbit );
}

IBitmapAPI::ERR CSparseBitmap::ErrDisableUpdates()
{
    m_cbitUpdate = 0;
    return IBitmapAPI::ERR::errSuccess;
}

IBitmapAPI::ERR CSparseBitmap::ErrSet( const size_t iBit, const BOOL fValue )
{
    if ( iBit >= m_cbitUpdate ) return IBitmapAPI::ERR::errInvalidParameter;
    BYTE* pb = (BYTE*)m_rgbit;
    const BYTE mask = (BYTE)( 1u << ( iBit & 7 ) );
    if ( fValue )   pb[ iBit >> 3 ] = (BYTE)( pb[ iBit >> 3 ] | mask );
    else            pb[ iBit >> 3 ] = (BYTE)( pb[ iBit >> 3 ] & ~mask );
    return IBitmapAPI::ERR::errSuccess;
}

IBitmapAPI::ERR CSparseBitmap::ErrGet( _In_ const size_t iBit, _Out_ BOOL* const pfValue )
{
    if ( iBit >= m_cbit ) return IBitmapAPI::ERR::errInvalidParameter;
    const BYTE* pb = (const BYTE*)m_rgbit;
    *pfValue = ( pb[ iBit >> 3 ] & ( 1u << ( iBit & 7 ) ) ) ? fTrue : fFalse;
    return IBitmapAPI::ERR::errSuccess;
}


//  Residence-map scan — collapsed to a stub. The engine uses this for a
//  one-shot "which pages of my own buffer are paged in?" query that maps
//  cleanly to mincore(); we expose a CFixedBitmap-backed implementation.

LOCAL DWORD g_dwResidenceUpdateId = 0;

ERR ErrOSMemoryPageResidenceMapScanStart( const size_t /*cbMax*/, _Out_ DWORD * const pdwUpdateId )
{
    *pdwUpdateId = ++g_dwResidenceUpdateId;
    return JET_errSuccess;
}

ERR ErrOSMemoryPageResidenceMapRetrieve( void* const pv, const size_t cb, IBitmapAPI** const ppbmapi )
{
    *ppbmapi = nullptr;
    if ( !pv || !cb ) return ErrERRCheck( JET_errInvalidParameter );

    const size_t cbPage = sysconf( _SC_PAGESIZE );
    const size_t cPage = ( cb + cbPage - 1 ) / cbPage;
    const ULONG cbBuffer = (ULONG)( ( cPage + 7 ) / 8 );
    BYTE* rgb = (BYTE*)PvOSMemoryHeapAlloc( cbBuffer );
    if ( !rgb ) return ErrERRCheck( JET_errOutOfMemory );

    unsigned char* rgvec = (unsigned char*)PvOSMemoryHeapAlloc( cPage );
    if ( !rgvec )
    {
        OSMemoryHeapFree( rgb );
        return ErrERRCheck( JET_errOutOfMemory );
    }

    BOOL fOk = ( mincore( pv, cb, rgvec ) == 0 );
    CFixedBitmap* pbm = new CFixedBitmap( rgb, cbBuffer );
    if ( !pbm )
    {
        OSMemoryHeapFree( rgvec );
        OSMemoryHeapFree( rgb );
        return ErrERRCheck( JET_errOutOfMemory );
    }
    pbm->ErrInitBitmap( cPage );
    if ( fOk )
    {
        for ( size_t i = 0; i < cPage; ++i )
        {
            pbm->ErrSet( i, ( rgvec[ i ] & 1 ) ? fTrue : fFalse );
        }
    }
    OSMemoryHeapFree( rgvec );
    *ppbmapi = pbm;
    //  Note: rgb is owned by *pbm; engine OSMemoryHeapFrees it via delete.
    return JET_errSuccess;
}

VOID OSMemoryPageResidenceMapScanStop()
{
}

VOID OSMemoryIPageResidenceMapPreinit() {}
VOID OSMemoryIPageResidenceMapPostterm() {}


//  COSMemoryMap — multi-mapped reserve/commit. Our Linux engine doesn't
//  exercise the multi-mapping case yet (used by debug-only paths), so
//  ErrOSMMReserve__ degenerates to a single mmap reservation and the
//  rgpvMap / rgfProtect arrays are honored only for index 0.

COSMemoryMap::COSMemoryMap()
    : m_pvMap( nullptr ),
      m_cbMap( 0 ),
      m_cMap( 0 ),
      m_cbReserve( 0 ),
      m_cbCommit( 0 )
#ifdef MEM_CHECK
    , m_posmmNext( NULL ),
      m_fInList( fFalse ),
      m_szFile( NULL ),
      m_lLine( 0 )
#endif
{}

COSMemoryMap::~COSMemoryMap()
{
    if ( m_pvMap )
    {
        munmap( m_pvMap, m_cbReserve );
        m_pvMap = nullptr;
    }
}

COSMemoryMap::ERR COSMemoryMap::ErrOSMMInit() { return COSMemoryMap::ERR::errSuccess; }
VOID COSMemoryMap::OSMMTerm() {}

BOOL COSMemoryMap::FCanMultiMap()
{
    //  Linux doesn't expose Windows-style file-section multi-mapping, so
    //  callers that need true multi-map support fall back to single-map.
    return fFalse;
}

COSMemoryMap::ERR COSMemoryMap::ErrOSMMReserve__(
    const size_t        cbMap,
    const size_t        cMap,
    __inout_ecount( cMap ) void** const     rgpvMap,
    const BOOL* const   /*rgfProtect*/ )
{
    if ( cMap == 0 || cbMap == 0 ) return COSMemoryMap::ERR::errOutOfAddressSpace;
    void* pv = mmap( nullptr, cbMap, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 );
    if ( pv == MAP_FAILED ) return COSMemoryMap::ERR::errOutOfAddressSpace;
    m_pvMap = pv;
    m_cbMap = cbMap;
    m_cMap = cMap;
    m_cbReserve = cbMap;
    rgpvMap[ 0 ] = pv;
    for ( size_t i = 1; i < cMap; ++i ) rgpvMap[ i ] = nullptr;
    return COSMemoryMap::ERR::errSuccess;
}

BOOL COSMemoryMap::FOSMMCommit( const size_t cbCommit )
{
    if ( !m_pvMap || cbCommit > m_cbMap ) return fFalse;
    if ( mprotect( m_pvMap, cbCommit, PROT_READ | PROT_WRITE ) != 0 ) return fFalse;
    m_cbCommit = cbCommit;
    return fTrue;
}

VOID COSMemoryMap::OSMMFree( void * const /*pv*/ )
{
    if ( m_pvMap )
    {
        munmap( m_pvMap, m_cbReserve );
        m_pvMap = nullptr;
        m_cbMap = m_cMap = m_cbReserve = m_cbCommit = 0;
    }
}

COSMemoryMap::ERR COSMemoryMap::ErrOSMMPatternAlloc__(
    const size_t    cbPattern,
    const size_t    cbSize,
    void** const    ppvPattern )
{
    if ( cbPattern == 0 || cbSize < cbPattern ) return COSMemoryMap::ERR::errOutOfMemory;
    void* pv = mmap( nullptr, cbSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    if ( pv == MAP_FAILED ) return COSMemoryMap::ERR::errOutOfMemory;
    m_pvMap = pv;
    m_cbMap = cbPattern;
    m_cMap = cbSize / cbPattern;
    m_cbReserve = cbSize;
    m_cbCommit = cbSize;
    *ppvPattern = pv;
    return COSMemoryMap::ERR::errSuccess;
}

VOID COSMemoryMap::OSMMPatternFree()
{
    OSMMFree( m_pvMap );
}


//  Lifecycle.

BOOL FOSMemoryPreinit()
{
    long cbPage = sysconf( _SC_PAGESIZE );
    if ( cbPage <= 0 ) cbPage = 4096;
    //  Match Windows' GetSystemInfo() shape: commit granularity = page size
    //  (4K), reserve/allocation granularity = 64K. Linux mmap is actually
    //  byte-granular for placement and only page-aligned for length, but the
    //  engine's resource manager assumes commit < reserve (cresmgr.cxx:794
    //  divides by `m_cbChunkSize / cbSectionSize` where cbSectionSize is the
    //  reserve granularity; equal granularities make m_cbChunkSize default
    //  to less than cbSectionSize and the divisor goes to zero → SIGFPE).
    g_dwPageReserveGran = 64 * 1024;
    g_dwPageCommitGran  = (DWORD)cbPage;

    struct sysinfo si = {};
    if ( sysinfo( &si ) == 0 )
    {
        g_cbMemoryTotal = (QWORD)si.totalram * (QWORD)si.mem_unit;
    }

    struct rlimit rl = {};
    if ( getrlimit( RLIMIT_AS, &rl ) == 0 && rl.rlim_cur != RLIM_INFINITY )
    {
        g_cbPageReserveTotal = (DWORD_PTR)rl.rlim_cur;
    }
    else
    {
        g_cbPageReserveTotal = (DWORD_PTR)g_cbMemoryTotal;
    }

    RegionRecord( nullptr, 0 );  //  touch the table so it's in BSS
    return fTrue;
}

void OSMemoryPostterm() {}

ERR ErrOSMemoryInit() { return JET_errSuccess; }

void OSMemoryTerm() {}
