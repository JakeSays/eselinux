// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Platform sink for admin events on Linux.
//
// OSEventReportEvent (event.cxx) formats every UtilReportEvent into a
// wide message via FormatMessageW, then calls OSEventEmitToOsLog
// (declared in os/event.hxx).  This file is the Linux implementation:
//
//   - syslog(3) is the primary path.  It's in libc, so it works on
//     every POSIX system regardless of whether systemd, rsyslogd,
//     syslog-ng, busybox-syslogd, or nothing at all is running.  On
//     systemd boxes the systemd-journald.socket forwarder captures
//     syslog automatically — admins find ESE events with
//     `journalctl SYSLOG_IDENTIFIER=ese`.
//
//   - sd_journal_sendv is an opportunistic upgrade.  We dlopen
//     libsystemd.so.0 on first use and look up the function.  If both
//     succeed we use it for every subsequent emit and attach
//     structured fields (ESE_EVENT_ID, ESE_CATEGORY, ESE_SOURCE) that
//     are searchable via `journalctl ESE_EVENT_ID=NNN`.  When the
//     library is absent (non-systemd box, container without
//     libsystemd, etc.) the syslog path stays in use.
//
// Either way, libese.so has no link-time dependency on libsystemd.
// The dlopen is best-effort; failure paths drop straight to syslog.

#include "osstd.hxx"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/uio.h>


namespace
{


//  -------- sd_journal_sendv discovery --------

typedef int ( *PfnSdJournalSendv )( const struct iovec * iov, int n );

PfnSdJournalSendv  g_pfnSdJournalSendv = nullptr;
void *             g_phLibsystemd      = nullptr;
pthread_once_t     g_onceInit          = PTHREAD_ONCE_INIT;

void InitOnce()
{
    //  Open libsystemd if it's on the box.  Failure is silent and
    //  expected — non-systemd systems (Alpine, Void, minimal
    //  containers) won't have it.  Either soname is acceptable; .0 is
    //  the canonical one shipped by every libsystemd-bearing distro
    //  this side of 2014.
    g_phLibsystemd = dlopen( "libsystemd.so.0", RTLD_LAZY | RTLD_LOCAL );
    if ( g_phLibsystemd == nullptr )
    {
        g_phLibsystemd = dlopen( "libsystemd.so", RTLD_LAZY | RTLD_LOCAL );
    }
    if ( g_phLibsystemd != nullptr )
    {
        g_pfnSdJournalSendv = (PfnSdJournalSendv)dlsym( g_phLibsystemd, "sd_journal_sendv" );
    }

    //  syslog(3) is always present; SYSLOG_IDENTIFIER fields land on
    //  the program name we pass here (via the `LOG_PID` flag the
    //  process pid is appended in classic /var/log/messages too).
    openlog( "ese", LOG_PID | LOG_NDELAY, LOG_USER );
}

void EnsureInit()
{
    pthread_once( &g_onceInit, InitOnce );
}


//  -------- wide → narrow conversion --------

//  Convert a NUL-terminated WCHAR* to a UTF-8 char* in caller-provided
//  scratch.  Returns the byte length (excluding the trailing NUL) or 0
//  on failure / empty input.  Uses the engine's WideCharToMultiByte
//  shim, so behaviour matches the rest of the OS layer.
size_t WideToUtf8( const WCHAR * wsz, char * buf, size_t cbBuf )
{
    if ( wsz == nullptr || cbBuf == 0 )
    {
        return 0;
    }
    const int cb = WideCharToMultiByte( CP_UTF8, 0, wsz, -1,
                                        buf, (int)cbBuf,
                                        nullptr, nullptr );
    if ( cb <= 1 )
    {
        //  cb==1 means "just the NUL".  Treat as empty.
        if ( cbBuf > 0 )
        {
            buf[ 0 ] = '\0';
        }
        return 0;
    }
    //  WideCharToMultiByte returns the count including the trailing
    //  NUL when the source was NUL-terminated.
    return (size_t)( cb - 1 );
}


//  -------- severity mapping --------

int SyslogPriorityForType( const EEventType type )
{
    switch ( type )
    {
        case eventError:
            return LOG_ERR;
        case eventWarning:
            return LOG_WARNING;
        case eventSuccess:
        case eventInformation:
        default:
            return LOG_INFO;
    }
}


//  -------- emit paths --------

//  Build the iovec array sd_journal_sendv expects.  Each entry is a
//  "KEY=VALUE" string — no trailing NUL is required and iov_len must
//  be the byte length of just KEY=VALUE.
void EmitViaJournald(
        const int      priority,
        const char *   szSource,
        const DWORD    catid,
        const DWORD    msgid,
        const char *   szText )
{
    //  Each line is small (~80 bytes max for the structured fields,
    //  plus the MESSAGE field which we cap at 8KB upstream).  Build
    //  the field strings on the stack.
    char szMessage  [ 8192 ];
    char szPriority [ 32 ];
    char szIdent    [ 32 ];
    char szEventId  [ 64 ];
    char szCategory [ 64 ];
    char szSrcField [ 256 ];

    const int cbMessage  = snprintf( szMessage,  sizeof( szMessage ),
                                     "MESSAGE=%s", szText != nullptr ? szText : "" );
    const int cbPriority = snprintf( szPriority, sizeof( szPriority ),
                                     "PRIORITY=%d", priority );
    const int cbIdent    = snprintf( szIdent,    sizeof( szIdent ),
                                     "SYSLOG_IDENTIFIER=ese" );
    const int cbEventId  = snprintf( szEventId,  sizeof( szEventId ),
                                     "ESE_EVENT_ID=%lu", (unsigned long)msgid );
    const int cbCategory = snprintf( szCategory, sizeof( szCategory ),
                                     "ESE_CATEGORY=%lu", (unsigned long)catid );
    const int cbSrcField = snprintf( szSrcField, sizeof( szSrcField ),
                                     "ESE_SOURCE=%s", szSource != nullptr ? szSource : "" );

    struct iovec rgiov[ 6 ];
    int          ciov = 0;

    if ( cbMessage > 0 )
    {
        rgiov[ ciov ].iov_base = szMessage;
        rgiov[ ciov ].iov_len  = (size_t)cbMessage;
        ++ciov;
    }
    if ( cbPriority > 0 )
    {
        rgiov[ ciov ].iov_base = szPriority;
        rgiov[ ciov ].iov_len  = (size_t)cbPriority;
        ++ciov;
    }
    if ( cbIdent > 0 )
    {
        rgiov[ ciov ].iov_base = szIdent;
        rgiov[ ciov ].iov_len  = (size_t)cbIdent;
        ++ciov;
    }
    if ( cbEventId > 0 )
    {
        rgiov[ ciov ].iov_base = szEventId;
        rgiov[ ciov ].iov_len  = (size_t)cbEventId;
        ++ciov;
    }
    if ( cbCategory > 0 )
    {
        rgiov[ ciov ].iov_base = szCategory;
        rgiov[ ciov ].iov_len  = (size_t)cbCategory;
        ++ciov;
    }
    if ( cbSrcField > 0 && szSource != nullptr )
    {
        rgiov[ ciov ].iov_base = szSrcField;
        rgiov[ ciov ].iov_len  = (size_t)cbSrcField;
        ++ciov;
    }

    (void)g_pfnSdJournalSendv( rgiov, ciov );
}


void EmitViaSyslog(
        const int      priority,
        const char *   szSource,
        const DWORD    msgid,
        const char *   szText )
{
    //  Plain syslog has no structured field surface, so we prefix the
    //  source name and msgid into the message body so admins reading
    //  /var/log/messages still get the context.
    if ( szSource != nullptr && szSource[ 0 ] != '\0' )
    {
        syslog( priority, "[%s id=%lu] %s",
                szSource,
                (unsigned long)msgid,
                szText != nullptr ? szText : "" );
    }
    else
    {
        syslog( priority, "[id=%lu] %s",
                (unsigned long)msgid,
                szText != nullptr ? szText : "" );
    }
}


} // anonymous namespace


void OSEventEmitToOsLog(
        const EEventType    type,
        const WCHAR *       szSourceEventKey,
        const CategoryId    catid,
        const MessageId     msgid,
        const WCHAR *       wszFormattedText )
{
    EnsureInit();

    char szText  [ 8192 ];
    char szSource[ 256  ];

    (void)WideToUtf8( wszFormattedText, szText,   sizeof( szText )   );
    (void)WideToUtf8( szSourceEventKey, szSource, sizeof( szSource ) );

    const int priority = SyslogPriorityForType( type );

    if ( g_pfnSdJournalSendv != nullptr )
    {
        EmitViaJournald( priority, szSource, catid, msgid, szText );
    }
    else
    {
        EmitViaSyslog( priority, szSource, msgid, szText );
    }
}
