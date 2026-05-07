// POSIX equivalent of os/osdisk.cxx. The full Win32 implementation orchestrates
// IOCP completion threads, an IOREQ pool, an oscillating IO heap, and emulated
// QoS scheduling — none of which apply to the synchronous, io_uring-bound
// design used on Linux.
//
// What we keep from the upstream file:
//
//   - QOS↔urgent-level math helpers (pure portable bit twiddling)
//   - QoS-driven max-outstanding IO computation (Smooth-ish ramp)
//   - OSFileIIOReportError that emits an event for failed IOs
//   - Lifecycle entry points (init/term/preinit/postterm)
//
// What we stub:
//
//   - ErrOSDiskIOREQReserve / OSDiskIOREQUnreserve — Linux file path is
//     synchronous-or-io_uring; there's no IOREQ pool to reserve from, so these
//     trivially succeed.
//   - The COSDisk class itself (only required by the upstream IO scheduler).
//
// As io_uring lands in Phase 6 the queueing/scheduling pieces of osdisk will
// migrate back in.

#include "osstd.hxx"


////////////////////////////////////////////////
//  IOREQ pool — stub
//
//  The reserve pool exists upstream so that a caller can guarantee an IOREQ
//  is available even if the global IOREQ pool is exhausted. With our
//  synchronous file layer there is no global IOREQ pool, so reserve is a
//  no-op success and unreserve is a no-op.

ERR ErrOSDiskIOREQReserve()
{
    return JET_errSuccess;
}

VOID OSDiskIOREQUnreserve()
{
}


////////////////////////////////////////////////
//  QOS ↔ urgent-level helpers (verbatim portable from osdisk.cxx)
//

OSFILEQOS QosOSFileFromUrgentLevel( _In_ const ULONG iUrgentLevel )
{
    Assert( iUrgentLevel >= 1 );
    Assert( iUrgentLevel <= qosIODispatchUrgentBackgroundLevelMax );

    DWORD grbitQOS = ( iUrgentLevel << qosIODispatchUrgentBackgroundShft );

    Assert( qosIODispatchUrgentBackgroundMask & grbitQOS );
    Assert( 0 == ( ~qosIODispatchUrgentBackgroundMask & grbitQOS ) );

    return grbitQOS;
}

ULONG IOSDiskIUrgentLevelFromQOS( _In_ const OSFILEQOS grbitQOS )
{
    Assert( qosIODispatchUrgentBackgroundMask & grbitQOS );
    Assert( 0 == ( ( ~qosIODispatchUrgentBackgroundMask & grbitQOS ) & qosIODispatchMask ) );

    ULONG iUrgentLevel = ( grbitQOS & qosIODispatchUrgentBackgroundMask ) >> qosIODispatchUrgentBackgroundShft;

    Assert( iUrgentLevel >= 1 );
    Assert( iUrgentLevel <= qosIODispatchUrgentBackgroundLevelMax );

    return iUrgentLevel;
}

LONG CioOSDiskIFromUrgentLevelSmoothish( _In_ const ULONG iUrgentLevel, _In_ const DWORD cioUrgentMaxMax )
{
    Assert( iUrgentLevel >= 1 );
    Assert( iUrgentLevel <= qosIODispatchUrgentBackgroundLevelMax );

    ULONG cioOutstanding = 0;

    const ULONG iRamp1End = ( qosIODispatchUrgentBackgroundLevelMax / 8 );
    const ULONG iRamp1Len = iRamp1End;
    const ULONG cioRamp1Max = UlBound( cioUrgentMaxMax / 128, 1, cioUrgentMaxMax );

    const ULONG iRamp2End = ( qosIODispatchUrgentBackgroundLevelMax / 4 );
    const ULONG iRamp2Len = iRamp2End - iRamp1End;
    const ULONG cioRamp2Max = ( ( cioUrgentMaxMax / 32 ) > ( cioRamp1Max ) ) ?
                                UlBound( cioUrgentMaxMax / 32 - cioRamp1Max, 1, cioUrgentMaxMax ) :
                                1;

    const ULONG iRamp3End = ( qosIODispatchUrgentBackgroundLevelMax / 2 );
    const ULONG iRamp3Len = iRamp3End - iRamp2End;
    const ULONG cioRamp3Max = ( ( cioUrgentMaxMax / 4 ) > ( cioRamp1Max + cioRamp2Max ) ) ?
                                UlBound( cioUrgentMaxMax / 4 - ( cioRamp1Max + cioRamp2Max ), 1, cioUrgentMaxMax ) :
                                1;

    const ULONG iRamp4End = ( qosIODispatchUrgentBackgroundLevelMax / 1 );
    const ULONG iRamp4Len = iRamp4End - iRamp3End;
    const ULONG cioRamp4Max = cioUrgentMaxMax - ( cioRamp1Max + cioRamp2Max + cioRamp3Max );

    if ( iUrgentLevel <= iRamp1End )
    {
        cioOutstanding = UlBound( iUrgentLevel * cioRamp1Max / iRamp1Len, 1, cioRamp1Max );
    }
    else if ( iUrgentLevel <= iRamp2End )
    {
        const ULONG iRelativeUrgency = iUrgentLevel - iRamp1End;
        cioOutstanding = UlBound( iRelativeUrgency * cioRamp2Max / iRamp2Len, 1, cioRamp2Max );
        cioOutstanding += cioRamp1Max;
    }
    else if ( iUrgentLevel <= iRamp3End )
    {
        const ULONG iRelativeUrgency = iUrgentLevel - iRamp2End;
        cioOutstanding = UlBound( iRelativeUrgency * cioRamp3Max / iRamp3Len, 1, cioRamp3Max );
        cioOutstanding += ( cioRamp1Max + cioRamp2Max );
    }
    else
    {
        const ULONG iRelativeUrgency = iUrgentLevel - iRamp3End;
        cioOutstanding = UlBound( iRelativeUrgency * cioRamp4Max / iRamp4Len, 1, cioRamp4Max );
        cioOutstanding += ( cioRamp1Max + cioRamp2Max + cioRamp3Max );
    }

    Assert( cioOutstanding >= 1 );
    Assert( cioOutstanding <= cioUrgentMaxMax );

    return cioOutstanding;
}

LONG CioOSDiskIFromUrgentLevel( _In_ const ULONG iUrgentLevel, _In_ const DWORD cioUrgentMaxMax )
{
    const ULONG cioMax = CioOSDiskIFromUrgentLevelSmoothish( iUrgentLevel, cioUrgentMaxMax );
    Assert( cioMax >= 1 );
    Assert( cioMax <= cioUrgentMaxMax );
    return (LONG)cioMax;
}

ULONG CioDefaultUrgentOutstandingIOMax( _In_ const ULONG cioOutstandingMax )
{
    return cioOutstandingMax / 2;
}

ULONG CioBackgroundIOLow( _In_ const ULONG cioBackgroundMax )
{
    return max( 1u, cioBackgroundMax / 20 );    // 5% floor at 1
}

//  Meted-op flighting knobs — engine sets these via FlightConcurrentMetedOps
//  in response to JET_paramFlight_* params.
LONG g_cioConcurrentMetedOpsMax     = 2;
LONG g_cioLowQueueThreshold         = 10;
TICK g_dtickStarvedMetedOpThreshold = 50;

void FlightConcurrentMetedOps( INT cioOpsMax, INT cioLowThreshold, TICK dtickStarvation )
{
    if ( cioOpsMax > 0 )
    {
        g_cioConcurrentMetedOpsMax = cioOpsMax;
    }
    g_cioLowQueueThreshold = cioLowThreshold;
    if ( dtickStarvation > 9 )
    {
        g_dtickStarvedMetedOpThreshold = dtickStarvation;
    }
}

LONG CioOSDiskIFromUrgentQOS( _In_ const OSFILEQOS grbitQOS, _In_ const DWORD cioUrgentMaxMax )
{
    const ULONG iUrgentLevel = IOSDiskIUrgentLevelFromQOS( grbitQOS );
    return CioOSDiskIFromUrgentLevel( iUrgentLevel, cioUrgentMaxMax );
}

LONG CioOSDiskPerfCounterIOMaxFromUrgentQOS( _In_ IFileSystemConfiguration* const pfsconfig, _In_ const OSFILEQOS grbitQOS )
{
    return CioOSDiskIFromUrgentQOS( grbitQOS, CioDefaultUrgentOutstandingIOMax( pfsconfig->CIOMaxOutstanding() ) );
}


////////////////////////////////////////////////
//  IO error event reporting
//
//  Posts a JET event describing a failed disk read/write so an admin can see it
//  in the OS event stream. This is the IFileSystemConfiguration* overload —
//  the COSFile* overload upstream forwards to this same function after looking
//  up the config from the file. Since our COSFile (in osfile_posix.cxx) does
//  the same forwarding, only this version needs to live here.

VOID OSFileIIOReportError(
    _In_ IFileSystemConfiguration* const    pfsconfig,
    _In_ const WCHAR* const                 wszAbsPath,
    _In_ const BOOL                         fWrite,
    _In_ const QWORD                        ibOffset,
    _In_ const DWORD                        cbLength,
    _In_ const ERR                          err,
    _In_ const DWORD                        errSystem,
    _In_ const QWORD                        cmsecIOElapsed )
{
    const ULONG     cwsz = 7;
    const WCHAR *   rgpwsz[ cwsz ];
    DWORD           irgpwsz = 0;
    WCHAR           wszOffset[ 64 ];
    WCHAR           wszLength[ 64 ];
    WCHAR           wszTimeElapsed[ 64 ];
    WCHAR           wszError[ 64 ];
    WCHAR           wszSystemError[ 64 ];

    OSStrCbFormatW( wszOffset, sizeof( wszOffset ), L"%I64i (0x%016I64x)", ibOffset, ibOffset );
    OSStrCbFormatW( wszLength, sizeof( wszLength ), L"%u (0x%08x)", cbLength, cbLength );
    OSStrCbFormatW( wszTimeElapsed, sizeof( wszTimeElapsed ), L"%I64u.%03I64u", cmsecIOElapsed / 1000, cmsecIOElapsed % 1000 );
    OSStrCbFormatW( wszError, sizeof( wszError ), L"%i (0x%08x)", err, err );
    OSStrCbFormatW( wszSystemError, sizeof( wszSystemError ), L"%u (0x%08x)", errSystem, errSystem );

    rgpwsz[ irgpwsz++ ] = wszAbsPath;
    rgpwsz[ irgpwsz++ ] = wszOffset;
    rgpwsz[ irgpwsz++ ] = wszLength;
    rgpwsz[ irgpwsz++ ] = wszError;
    rgpwsz[ irgpwsz++ ] = wszSystemError;
    rgpwsz[ irgpwsz++ ] = L"";  //  no FormatMessageW description on Linux
    rgpwsz[ irgpwsz++ ] = wszTimeElapsed;

    Assert( irgpwsz <= cwsz );
    pfsconfig->EmitEvent(   eventError,
                            GENERAL_CATEGORY,
                            fWrite ? OSFILE_WRITE_ERROR_ID : OSFILE_READ_ERROR_ID,
                            irgpwsz,
                            rgpwsz,
                            JET_EventLoggingLevelMin );
}


////////////////////////////////////////////////
//  Lifecycle
//
//  ErrOSDiskInit/OSDiskTerm wire up the IOREQ pool and IO thread upstream.
//  Both are no-ops here; storage threads come from CTaskManager and io_uring
//  manages its own completion ring.

void OSDiskPostterm()       {}
BOOL FOSDiskPreinit()       { return fTrue; }
void OSDiskTerm()           {}
ERR  ErrOSDiskInit()        { return JET_errSuccess; }
