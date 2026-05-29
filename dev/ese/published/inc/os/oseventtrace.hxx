// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _OS_EVENT_TRACE_HXX_INCLUDED
#define _OS_EVENT_TRACE_HXX_INCLUDED


//  Include autogen header (provided Event Tracing Enums)
//
#include "oseventtrace.g.hxx"

//  Event Tracing Raw Impl API
//

void __cdecl OSEventTrace_( const ULONG etguid,
                            const size_t cData = 0,
                            ... );

// FOSEventTraceEnabled() / FOSEventTraceEnabled<etguid>() are the
// upstream ETW gates; on the Linux port their bodies are inline here.
//
// The non-templated overload is no longer what the OSEventTrace macro
// gates on — it stays fTrue purely as a harmless default for any
// non-macro caller.  The macro now gates per-event at runtime via
// PosixTraceEnabled(etguid) (below), so an event whose lttng tracepoint
// no session has enabled skips OSEventTrace_ and all of its argument
// marshalling — the same role upstream's EventEnabled()-backed gate
// played on ETW.
//
// The templated overload still backs the per-guid
// COSEventTraceIdCheck::FAnnounceTime<etguid>() station-identification
// logic; its Linux body stays fFalse (those announce paths do expensive
// pre-trace data gathering and are wired separately).  The explicit
// template instantiations in oseventtrace_posix.cxx bind to this body.
INLINE BOOL FOSEventTraceEnabled() { return fTrue; }

template< OSEventTraceGUID etguid >
INLINE BOOL FOSEventTraceEnabled() { return fFalse; }

// Runtime per-event gate.  Returns fTrue when a loaded lttng session has
// enabled the tracepoint for this etguid.  Defined in
// os/posix/oseventtrace_posix.cxx; reads the provider's per-tracepoint
// enable-state pointer published through the EseTracepointTable.
BOOL PosixTraceEnabled( const ULONG etguid );

#define OSEventTrace( etguid, ... ) \
    if ( PosixTraceEnabled( etguid ) ) OSEventTrace_( ( etguid ) __VA_OPT__(,) __VA_ARGS__ )

//  The first 8 are generic reasons, resused per event, the next 248 are for whatever 

enum TraceStationIdentificationReason : BYTE // tsidr
{
    tsidrInvalid = 0,

    //  Generic Reasons
    //
    tsidrPulseInfo = 1,             //  For regularly repeatedly trace attempt and should be suppressed with COSEventTraceIdCheck

    tsidrOpenInit = 2,              //  Open or initialization of the object.
    tsidrCloseTerm = 3,             //  Close or term of the object.

    tsidrGenericMax = 16,

    //  OS Sub-Component Specific Reasons
    //
    tsidrFileRenameFile             = 17,
    tsidrFileChangeEngineFileType   = 18,

    //  Engine Sub-Component Specific Reasons
    //
    tsidrEngineInstBeginRedo        = 65,
    tsidrEngineInstBeginUndo        = 66,
    tsidrEngineInstBeginDo          = 67,
    tsidrEngineFmpDbHdr1st          = 68,   //  the first read of header during attach
};

class COSEventTraceIdCheck
{
private:
    BOOL                m_fTracedStationIdentification;
#ifdef DEBUG
    OSEventTraceGUID    m_etguidCheck;
#endif

public:
    COSEventTraceIdCheck()
    {
        m_fTracedStationIdentification = fFalse;
        OnDebug( m_etguidCheck = (OSEventTraceGUID)-1 );
    }

    template< OSEventTraceGUID etguid >
    INLINE BOOL FAnnounceTime( const TraceStationIdentificationReason tsidr )
    {
        Expected( etguid );
        Assert( m_etguidCheck == (OSEventTraceGUID)-1 || m_etguidCheck == etguid ); // only one guid per trace announcer supression object
        OnDebug( m_etguidCheck = etguid );


        if ( tsidr != tsidrPulseInfo &&
                tsidr != tsidrOpenInit )
        {
            //  Not the type of station id reason we understand / suppress for.
            return fTrue;
        }

        //  Note we let tsidrOpenInit through to set the initial state of this structure ... doing so 
        //  avoids a dup tsidrPulseInfo tracing when logging is already on during tsidrOpenInit station 
        //  identification, but we always return true for tsidrOpenInit regardless.

        const BOOL fTracingOn = FOSEventTraceEnabled< etguid >();


        //  This trickiness won't be 100% if you have like two overlapping ETW traces, but if we need to start handling
        //  that we can simply make the "Station Identification" repeat every 10 or so minutes.  Or perhaps even ETW can
        //  tell us how many trace watchers, then we could re-issue any time it increases.

        if ( !fTracingOn && m_fTracedStationIdentification )
        {
            // Tracing off, must reset to off, so if someone re-enabled later, we'll re-station identify ourselves ...
            AtomicCompareExchange( (LONG*)&m_fTracedStationIdentification, (LONG)fTrue, (LONG)fFalse );
        }
        if ( fTracingOn && !m_fTracedStationIdentification )
        {
            BOOL fStationIDd = (BOOL) AtomicCompareExchange( (LONG*)&m_fTracedStationIdentification, (LONG)fFalse, (LONG)fTrue );
            if ( fStationIDd == fFalse )
            {
                //  This means we won, so tell the caller to show their papers.
                return fTrue;
            }
        }

        //  Always announce open.
        return tsidr == tsidrOpenInit;
    }
    
};

//  Requires explicit instantiation, to define in OS static library so it can link into ese.dll until C++20 comes
//  along.  I (SOMEONE) am convinced I'm being strung along by a plot by SOMEONE & SOMEONE to get me onto a modern 
//  compiler, because I am old and curmudgeonly!
// 

//  These are used independently for a couple special traces
extern template BOOL FOSEventTraceEnabled< _etguidCacheRequestPage >();
extern template BOOL FOSEventTraceEnabled< _etguidCacheMemoryUsage >();

//  These are used by the next set of ::FAnnounceTime() templates (keep same order)
extern template BOOL FOSEventTraceEnabled< _etguidInstStationId >();
extern template BOOL FOSEventTraceEnabled< _etguidDiskStationId >();
extern template BOOL FOSEventTraceEnabled< _etguidFileStationId >();
extern template BOOL FOSEventTraceEnabled< _etguidSysStationId >();
extern template BOOL FOSEventTraceEnabled< _etguidIsamDbfilehdrInfo >();
extern template BOOL FOSEventTraceEnabled< _etguidFmpStationId >();

extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidInstStationId >( const TraceStationIdentificationReason tsidr );
extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidDiskStationId >( const TraceStationIdentificationReason tsidr );
extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidFileStationId >( const TraceStationIdentificationReason tsidr );
extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidSysStationId >( const TraceStationIdentificationReason tsidr );
extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidIsamDbfilehdrInfo >( const TraceStationIdentificationReason tsidr );
extern template BOOL COSEventTraceIdCheck::FAnnounceTime< _etguidFmpStationId >( const TraceStationIdentificationReason tsidr );

#endif  //  _OS_EVENT_TRACE_HXX_INCLUDED

