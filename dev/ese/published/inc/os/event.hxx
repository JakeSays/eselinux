// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#ifndef _OS_EVENT_HXX_INCLUDED
#define _OS_EVENT_HXX_INCLUDED


//  Event Logging

typedef DWORD CategoryId;
typedef DWORD MessageId;

enum EEventType
{
    eventSuccess = 0,
    eventError = 1,
    eventWarning = 2,
    eventInformation = 4,
};

enum EEventFacility // eventfacility
{
    eventfacilityNone               = 0x00,
    eventfacilityReportOsEvent      = 0x01, //  the regular OS application (or redirected via source param) event logs
    eventfacilityOsDiagTracking     = 0x02,
    eventfacilityRingBufferCache    = 0x04,
    eventfacilityOsEventTrace       = 0x08,
    eventfacilityOsTrace            = 0x10,
};
DEFINE_ENUM_FLAG_OPERATORS_BASIC( EEventFacility );


void OSEventReportEvent(
        const WCHAR *       szEventSourceKey,
        const EEventFacility eventfacility,
        const EEventType    type,
        const CategoryId    catid,
        const MessageId     msgid,
        const DWORD         cString,
        const WCHAR *       rgpszString[],
        const DWORD         cDataSize = 0,
        void *              pvRawData = nullptr );

//  Platform sink for fully-formatted admin events.  OSEventReportEvent
//  calls this after FormatMessageW has produced the human-readable text.
//
//  Linux: forwards through syslog(3) — captured by journald on systemd
//  boxes, by rsyslogd/syslog-ng elsewhere — and additionally tries
//  sd_journal_sendv via a libsystemd dlopen for structured fields when
//  journald is available.  Implemented in os/posix/eventoslog_posix.cxx.
//
//  Windows: stays empty; the Win32 ReportEventW path inside
//  OSEventReportEvent is the canonical sink.

#ifdef ESE_OS_LINUX

void OSEventEmitToOsLog(
        const EEventType    type,
        const WCHAR *       szSourceEventKey,
        const CategoryId    catid,
        const MessageId     msgid,
        const WCHAR *       wszFormattedText );

#else

INLINE void OSEventEmitToOsLog(
        const EEventType,
        const WCHAR *,
        const CategoryId,
        const MessageId,
        const WCHAR * )
{
}

#endif

#ifdef OS_LAYER_VIOLATIONS
#include "eventu.hxx"

//  We further include the jetmsg.h here, because the rest of ESE expects it 
//  from here, and there were issues moving it around.  Ideally, we would make
//  make it so this is only included for ESE itself.  Maybe for the oslayer
//  compile as well, as it throws events.
//
#include "_jetmsg.h"

#endif

#endif  //  _OS_EVENT_HXX_INCLUDED

