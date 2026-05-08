// POSIX equivalent of os/osfile.cxx. Upstream is ~3200 lines of NTFS/IOCP-
// flavoured COSFile, _OSFILE, MM-cache, ReadCopyNumber, FSCTL/DeviceIoControl
// machinery. The Phase 5 milestone needs link-clean stubs; the io_uring-backed
// COSFile lands in Phase 6.
//
// What we provide:
//
//   - Lifecycle entry points (preinit/init/term/postterm)
//   - ErrIOWriteContiguous / ErrIORetrieveSparseSegmentsInRegion: top-level
//     helpers that call IFileAPI methods. They're written entirely in terms of
//     the abstract interface so we keep them verbatim from upstream.

#include "osstd.hxx"
#include "iouring_posix.hxx"


////////////////////////////////////////////////
//  Zero-extend buffer globals + COSLayerPreInit::SetZeroExtend
//
//  Engine call sites use g_rgbZero as the source pointer for log-extend
//  pwrite()s (and other zero-fill IO) — see logwrite.cxx and io.cxx. The
//  buffer is allocated lazily during ErrOSFileInit so we know g_cbZero is
//  final. SetZeroExtend may grow the desired size, but the engine resizes
//  via FOSFileExtendCacheZeroBufferSize that we don't yet implement;
//  starting at 1 MB matches the upstream default and is plenty for v1.

QWORD           g_cbZero    = 1024 * 1024;   // 1 MB default extension
BYTE*           g_rgbZero   = nullptr;          // allocated by ErrOSFileInit


////////////////////////////////////////////////
//  Lifecycle

void OSFilePostterm()
{
}

BOOL FOSFilePreinit()
{
    return fTrue;
}

void OSFileTerm()
{
    osposix::IOUringTerm();
    if ( g_rgbZero )
    {
        OSMemoryPageFree( g_rgbZero );
        g_rgbZero = nullptr;
    }
}

ERR  ErrOSFileInit()
{
    if ( !g_rgbZero )
    {
        g_rgbZero = (BYTE*)PvOSMemoryPageAlloc( (size_t)g_cbZero, nullptr );
        if ( !g_rgbZero )
        {
            return ErrERRCheck( JET_errOutOfMemory );
        }
    }
    return osposix::ErrIOUringInit();
}

void COSLayerPreInit::SetZeroExtend( QWORD cbZeroExtend )
{
    if ( cbZeroExtend < OSMemoryPageCommitGranularity() )
    {
        // leave at existing alignment
    }
    else if ( cbZeroExtend < OSMemoryPageReserveGranularity() )
    {
        cbZeroExtend -= cbZeroExtend % OSMemoryPageCommitGranularity();
    }
    else
    {
        cbZeroExtend -= cbZeroExtend % OSMemoryPageReserveGranularity();
    }
    g_cbZero = cbZeroExtend;
}


////////////////////////////////////////////////
//  ErrIOWriteContiguous / ErrIORetrieveSparseSegmentsInRegion
//
//  These helpers are pure IFileAPI clients — they don't touch _OSFILE,
//  COSDisk, or COSFile internals — so the upstream code carries straight
//  across. The only difference is that we use the public COSFile::CIOComplete
//  declared in osfileapi.hxx via IFileAPI rather than the internal one.

namespace {

// Local CIOComplete equivalent of COSFile::CIOComplete used purely as a
// completion-context for asynchronous writes issued via IFileAPI::ErrIOWrite.
class CIOComplete
{
    public:
        CIOComplete()
            :   m_msig( CSyncBasicInfo( "CIOComplete::m_msig" ) ),
                m_err( JET_errSuccess )
        {
        }

        void Complete( const ERR err )
        {
            m_err = err;
            m_msig.Set();
        }

        void Wait()
        {
            m_msig.Wait();
        }

        ERR             m_err;

    private:
        CManualResetSignal  m_msig;
};

void IOWriteContiguousComplete_(    const ERR                       err,
                                    IFileAPI* const                 /* pfapi */,
                                    const FullTraceContext&         /* tc */,
                                    const OSFILEQOS                 /* grbitQOS */,
                                    const QWORD                     /* ibOffset */,
                                    const DWORD                     /* cbData */,
                                    const BYTE* const               /* pbData */,
                                    const DWORD_PTR                 keyIOComplete )
{
    CIOComplete* const piocomplete = reinterpret_cast< CIOComplete* >( keyIOComplete );
    if ( piocomplete )
    {
        piocomplete->Complete( err );
    }
    else
    {
        AssertSz( fFalse, "Async IO CIOComplete shoud not be NULL!" );
    }
}

} // anonymous

ERR ErrIOWriteContiguous(   IFileAPI* const                         pfapi,
                            __in_ecount( cData ) const TraceContext rgtc[],
                            const QWORD                             ibOffset,
                            __in_ecount( cData ) const DWORD        rgcbData[],
                            __in_ecount( cData ) const BYTE* const  rgpbData[],
                            const size_t                            cData,
                            const OSFILEQOS                         grbitQOS )
{
    ERR err = JET_errSuccess;
    BOOL fAllocatedFromHeap = fFalse;

    Assert( qosIODispatchMask & grbitQOS );
    Assert( !( pfapi->Fmf() & IFileAPI::fmfReadOnlyClient ) );
    Expected( !( pfapi->Fmf() & IFileAPI::fmfReadOnly ) );

    Expected( !( grbitQOS & qosIOOptimizeCombinable ) );

    Assert( 0 == ( qosIOCompleteMask & grbitQOS ) );

    CIOComplete  rgStackIoComplete[2];
    CIOComplete* rgIoComplete = rgStackIoComplete;

    if ( cData > _countof(rgStackIoComplete) )
    {
        rgIoComplete = new CIOComplete[cData];
        if ( rgIoComplete == nullptr )
        {
            Call( ErrERRCheck( JET_errOutOfMemory ) );
        }
        fAllocatedFromHeap = fTrue;
    }

    QWORD ibOffsetCurrent = ibOffset;
    size_t iData = 0;
    for ( iData = 0; iData < cData; ++iData )
    {
        Assert( rgcbData[iData] );
        TraceContextScope tcScope;
        *tcScope = rgtc[ iData ];
        err = pfapi->ErrIOWrite(    *tcScope,
                                    ibOffsetCurrent,
                                    rgcbData[iData],
                                    rgpbData[iData],
                                    grbitQOS | ( iData == 0 ? 0 : qosIOOptimizeCombinable ),
                                    IFileAPI::PfnIOComplete( IOWriteContiguousComplete_ ),
                                    DWORD_PTR( &rgIoComplete[iData] ) );
        Assert( err != errDiskTilt );
        if ( err != JET_errSuccess )
        {
            break;
        }
        ibOffsetCurrent += rgcbData[iData];
    }
    if ( iData > 0 )
    {
        CallS( pfapi->ErrIOIssue() );
    }
    for ( size_t i = 0; i < iData; ++i )
    {
        rgIoComplete[i].Wait();
    }
    if ( iData == cData )
    {
        for ( size_t j = 0; j < cData; ++j )
        {
            Call( rgIoComplete[j].m_err );
        }
    }
    CallSx( err, wrnIOSlow );

HandleError:
    if ( fAllocatedFromHeap )
    {
        Assert( rgIoComplete != rgStackIoComplete );
        Assert( rgIoComplete != NULL );
        delete [] rgIoComplete;
        rgIoComplete = nullptr;
    }
    if ( err == wrnIOSlow || err ==  JET_errSuccess )
    {
        return ( ( qosIOSignalSlowSyncIO & grbitQOS ) ) ? err : JET_errSuccess;
    }
    else
    {
        Assert( err < 0 );
        return err;
    }
}

ERR ErrIORetrieveSparseSegmentsInRegion(    IFileAPI* const                             pfapi,
                                            _In_ QWORD                                  ibFirst,
                                            _In_ QWORD                                  ibLast,
                                            _Inout_ CArray<SparseFileSegment>* const    parrsparseseg )
{
    ERR err = JET_errSuccess;

    Expected( ibLast >= ibFirst );
    Assert( parrsparseseg != NULL );
    Expected( parrsparseseg->Size() == 0 );

    for ( QWORD ib = ibFirst; ib <= ibLast; )
    {
        QWORD ibAlloc = 0, cbAlloc = 0;
        Call( pfapi->ErrRetrieveAllocatedRegion( ib, &ibAlloc, &cbAlloc ) );

        if ( ( ibAlloc > ib ) || ( cbAlloc == 0 ) )
        {
            SparseFileSegment sparseseg;
            sparseseg.ibFirst = ib;
            if ( cbAlloc == 0 )
            {
                Assert( ibAlloc == 0 );
                sparseseg.ibLast = ibLast;
            }
            else
            {
                Assert( ibAlloc > ib );
                sparseseg.ibLast = min( ibAlloc - 1, ibLast );
            }

            Call( ( parrsparseseg->ErrSetEntry( parrsparseseg->Size(), sparseseg ) == CArray<SparseFileSegment>::ERR::errSuccess ) ?
                                                                                      JET_errSuccess :
                                                                                      ErrERRCheck( JET_errOutOfMemory ) );
        }
        else
        {
            Assert( ibAlloc == ib );
            Assert( cbAlloc != 0 );
        }

        ib = ( cbAlloc != 0 ) ? ( ibAlloc + cbAlloc ) : ( ibLast + 1 );
    }

HandleError:

#ifdef DEBUG
    for ( size_t isparseseg = 0; isparseseg < parrsparseseg->Size(); isparseseg++ )
    {
        const SparseFileSegment& sparseseg = (*parrsparseseg)[isparseseg];
        Assert( ibFirst <= ibLast );
        Assert( sparseseg.ibFirst <= sparseseg.ibLast );
        Assert( sparseseg.ibFirst >= ibFirst );
        Assert( sparseseg.ibLast <= ibLast );
        if ( isparseseg > 0 )
        {
            Assert( sparseseg.ibFirst > (*parrsparseseg)[isparseseg - 1].ibLast );
        }
    }
#endif

    return err;
}
