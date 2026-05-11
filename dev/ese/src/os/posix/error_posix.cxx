// POSIX equivalent of error.cxx. The portable subsystems (TEST_INJECTION,
// FAULT_INJECTION, CONFIGOVERRIDE_INJECTION, HANG_INJECTION, RFS2, the
// per-thread last-error frame, ErrERRCheck_, the issue-source formatter,
// the cleanup-state TLS bit) are ported verbatim. Windows-only surfaces
// (MessageBoxW, AeDebug, RaiseFailFastException, CreateProcessW, SEH
// __try/__except, FormatMessageW, the WER and UnhandledExceptionFilter
// hooks) collapse to the obvious POSIX equivalents — abort(), __builtin_trap(),
// fwrite-to-stderr — or to no-ops where the engine treats them as best-effort.

#include "osstd.hxx"
#include "trace.hxx"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//  global state for exceptions/asserts

CRITICAL_SECTION    g_csError;
BOOL                g_fCritSecErrorInit = fFalse;

DWORD               g_tidAssertFired = 0x0;
CErrFrameSimple     g_cerrDoubleAssert;         // protected by g_csError

//  these should only be accessed while g_csError is held
#ifdef DEBUG
WCHAR               g_wszAssertTextFull[1024];
#endif
DWORD               g_fSkipAssert = fFalse;


//  The published header declares this `__forceinline`; on clang that maps
//  to `inline __attribute__((always_inline))` and clang refuses to emit a
//  callable symbol. Other TUs only see the forward declaration and emit an
//  external call. Force emission of an out-of-line body here with `used` +
//  `noinline`, which overrides the header's `always_inline` hint.
__attribute__((used, noinline))
CErrFrameSimple * PefLastThrow()
{
    return Postls() ? ( &(Postls()->m_efLastErr) ) : nullptr;
}

ULONG UlLineLastCall()
{
#ifdef DEBUG
    return Postls() ? Postls()->ulLineLastCall : 0;
#else
    return 0;
#endif
}


// ============================================================================================================
//  Stubs for Windows-only error/diagnostic surfaces
// ============================================================================================================

INT UtilMessageBoxW( _In_ const WCHAR * const /* wszText */,
                     _In_ const WCHAR * const /* wszCaption */,
                     _In_ const UINT /* uType */ )
{
    //  no GUI on the Linux build; behave like MB_ABORTRETRYIGNORE returning 0 so
    //  callers fall through to non-interactive paths (terminate, break, etc.).
    return 0;
}

BOOL IsDebuggerAttached()
{
    return fFalse;
}

BOOL IsDebuggerAttachable()
{
    return fFalse;
}

void KernelDebugBreakPoint()
{
    __builtin_trap();
}

void UserDebugBreakPoint()
{
    __builtin_trap();
}

VOID OSErrorRegisterForWer( VOID * /* pv */, DWORD /* cb */ )
{
    //  no WER on Linux; the engine treats this as best-effort
}

static void RaiseFailFastException()
{
    //  process-terminating fail-fast: bypass any C++ unwinding and SIGABRT.
    abort();
}

//  The Windows version uses SEH __try/__except so that g_fSkipAssert can resume
//  past the raised exception under a debugger. There is no equivalent on POSIX:
//  if g_fSkipAssert is set we simply return.

LOCAL void RaiseFailFastExceptionCheckingSkipAssert()
{
    if ( g_fSkipAssert )
    {
        g_fSkipAssert = fFalse;
        return;
    }
    abort();
}


#if defined( DEBUG ) || defined( MEM_CHECK ) || defined( ENABLE_EXCEPTIONS )
const WCHAR wszAssertFile[]     = L"assert.txt";
const WCHAR wszAssertCaption[]  = L"JET Assertion Failure";
#endif

UINT g_wAssertAction = JET_AssertFailFast;
UINT g_wExceptionAction = JET_ExceptionFailFast;
BOOL g_fSkipFailFast = fFalse;

UINT COSLayerPreInit::SetAssertAction( UINT wAssertAction )
{
    UINT wOriginalValue = g_wAssertAction;
    g_wAssertAction = wAssertAction;
    return wOriginalValue;
}

UINT COSLayerPreInit::SetExceptionAction( const UINT wExceptionAction )
{
    const UINT wOriginalValue = g_wExceptionAction;
    g_wExceptionAction = wExceptionAction;
    return wOriginalValue;
}

INT g_fNoWriteAssertEvent = 0;


// ============================================================================================================
//  Issue source formatter (portable)
// ============================================================================================================

#define CCH_MAX_SHORT_FILENAME  (30)
#define CCH_32_BIT_MAX          (11)
static const ULONG g_cchIssueSourceMax = 40 +
                                          CCH_MAX_SHORT_FILENAME * 2 +
                                          CCH_32_BIT_MAX * 12;

VOID ERRFormatIssueSource( __out_bcount( cbIssueSource ) WCHAR * wszIssueSource,
                           _In_ const ULONG cbIssueSource,
                           const DWORD dwSavedGLE,
                           _In_ PCSTR szFilename,
                           _In_ const LONG lLine )
{
    const CHAR * szFilenameSourcePre  = "";
    const CHAR * szFilenameSource     = SzSourceFileName( szFilename );

    const CHAR * szFilenameLastErrPre = "";
    const CHAR * szFilenameLastErr    = "";
    if ( FOSLayerUp() && PefLastThrow() )
    {
        szFilenameLastErr = SzSourceFileName( PefLastThrow()->SzFile() );
    }
    const ERR errLast      = PefLastThrow() ? PefLastThrow()->Err()    : 0;
    const ULONG lErrLastLine = PefLastThrow() ? PefLastThrow()->UlLine() : 0;

    const CHAR * const szDotDotDot = "...";

    if ( strlen( szFilenameSource ) > CCH_MAX_SHORT_FILENAME )
    {
        const ULONG cch = strlen( szFilenameSource );
        szFilenameSource    = &( szFilenameSource[cch - CCH_MAX_SHORT_FILENAME + strlen(szDotDotDot)] );
        szFilenameSourcePre = szDotDotDot;
    }

    if ( strlen( szFilenameLastErr ) > CCH_MAX_SHORT_FILENAME )
    {
        const ULONG cch = strlen( szFilenameLastErr );
        szFilenameLastErr    = &( szFilenameLastErr[cch - CCH_MAX_SHORT_FILENAME + strlen(szDotDotDot)] );
        szFilenameLastErrPre = szDotDotDot;
    }

    OSStrCbFormatW( wszIssueSource, cbIssueSource,
                    L"PV: %u.%u.%u.%u SV: %u.%u.%u.%u GLE: %u ERR: %d(%hs%hs:%u): %hs%hs(%u)",
                    DwUtilImageVersionMajor(), DwUtilImageVersionMinor(),
                    DwUtilImageBuildNumberMajor(), DwUtilImageBuildNumberMinor(),
                    DwUtilSystemVersionMajor(), DwUtilSystemVersionMinor(),
                    DwUtilSystemBuildNumber(), DwUtilSystemServicePackNumber(),
                    dwSavedGLE,
                    errLast, szFilenameLastErrPre, szFilenameLastErr, lErrLastLine,
                    szFilenameSourcePre, szFilenameSource, lLine );
}


// ============================================================================================================
//  AssertFail
// ============================================================================================================

#ifdef DEBUG

LOCAL DWORD g_pidAssert;
LOCAL DWORD g_tidAssert;
LOCAL const CHAR * g_szFilenameAssert;
LOCAL LONG g_lLineAssert;

LOCAL DWORD g_fDebuggerPrint = fTrue;

void OSDebugPrint( const WCHAR * const wszOutput )
{
    //  Linux equivalent: write the (UTF-16 short-wchar) text to stderr by
    //  narrowing best-effort. Used purely diagnostically.
    if ( g_fDebuggerPrint && wszOutput )
    {
        for ( const WCHAR* p = wszOutput; *p; p++ )
        {
            unsigned char ch = (unsigned char)( *p & 0xFF );
            fputc( ch >= 0x20 || ch == '\r' || ch == '\n' || ch == '\t' ? ch : '?', stderr );
        }
    }
}

void HandleNestedAssert( WCHAR const * szMessageFormat, char const * szFilename, LONG lLine )
{
    OSDebugPrint( L"\n\nDOUBLE ASSERT: " );
    OSDebugPrint( szMessageFormat );
    OSDebugPrint( L"\nSee g_cerrDoubleAssert for file/line info.\n" );
    g_cerrDoubleAssert.Set( szFilename, lLine, -1 );
    KernelDebugBreakPoint();
}

void __stdcall AssertFail( PCSTR szMessageFormat, PCSTR szFilename, LONG lLine, ... )
{
    va_list args;
    va_start( args, lLine );

    if ( g_wAssertAction == JET_AssertSkipAll )
    {
        va_end( args );
        return;
    }

    CHAR szAssertText[500];
    CHAR szAssertAddlInfo[500];

    DWORD dwSavedGLE = GetLastError();

    EnterCriticalSection( &g_csError );

    if ( g_tidAssertFired == DwUtilThreadId() )
    {
        SetLastError( dwSavedGLE );
        WCHAR wszMessageFormat[ _MAX_PATH ];
        (void)ErrOSSTRAsciiToUnicode( szMessageFormat,
                                      wszMessageFormat,
                                      _countof( wszMessageFormat ) );
        HandleNestedAssert( wszMessageFormat, szFilename, lLine );

        LeaveCriticalSection( &g_csError );
        SetLastError( dwSavedGLE );
        va_end( args );
        return;
    }

    g_tidAssertFired = DwUtilThreadId();

    g_szFilenameAssert = szFilename;
    g_lLineAssert = lLine;

    szFilename = SzSourceFileName( szFilename );

    INT offset = 0;
    OSStrCbFormatA( szAssertText + offset,
                    sizeof( szAssertText ) - offset * sizeof( szAssertText[0] ),
                    "Assertion Failure: " );
    offset = strlen( szAssertText );

    OSStrCbVFormatA( szAssertText + offset,
                     sizeof( szAssertText ) - offset,
                     szMessageFormat,
                     args );
    const CHAR * const szAssertMessage = szAssertText + offset;

    WCHAR wszIssueSource[ g_cchIssueSourceMax ] = L"FORMAT STRING FAIL";
    C_ASSERT( _countof( wszIssueSource ) < 260 );
    if ( FOSDllUp() )
    {
        ERRFormatIssueSource( wszIssueSource, sizeof( wszIssueSource ), dwSavedGLE, szFilename, lLine );
    }

    offset = 0;
    OSStrCbFormatA( szAssertAddlInfo + offset,
                    sizeof( szAssertAddlInfo ) - offset,
                    "PID: %d (0x%x), TID: 0x%x \r\n\r\n",
                    DwUtilProcessId(),
                    DwUtilProcessId(),
                    DwUtilThreadId() );
    offset = strlen( szAssertText );

    OSStrCbFormatA( szAssertAddlInfo + offset,
                    sizeof( szAssertAddlInfo ) - offset,
                    "\r\nComplete information can be found in:  %ws\r\n",
                    wszAssertFile );
    offset = strlen( szAssertText );

    OSStrCbFormatW( g_wszAssertTextFull, sizeof( g_wszAssertTextFull ),
                    L"%hs\r\n%ws\r\n%hs",
                    szAssertText,
                    wszIssueSource,
                    szAssertAddlInfo );

    OSTrace( JET_tracetagAsserts,
             OSFormat( "%hs\r\n%ws\r\n%hs", szAssertText, wszIssueSource, szAssertAddlInfo ) );

    if ( !g_fNoWriteAssertEvent && FOSDllUp() )
    {
        const WCHAR * rgszT[] = { g_wszAssertTextFull };
        UtilReportEvent( eventError, GENERAL_CATEGORY, PLAIN_TEXT_ID, 1, rgszT );
    }

    OSDiagTrackAssertFail( szAssertMessage, wszIssueSource );

    {
    CPRINTFFILE cprintffileAssertTxt( wszAssertFile );
    cprintffileAssertTxt( "%ws", g_wszAssertTextFull );
    }

    OSDebugPrint( g_wszAssertTextFull );

    UINT wAssertAction = g_wAssertAction;

    if ( wAssertAction == JET_AssertExit )
    {
        _exit( ~0 );
    }
    else if ( wAssertAction == JET_AssertBreak )
    {
        OSDebugPrint( L"\nTo continue, press 'g'.\n\n" );
        SetLastError( dwSavedGLE );
        KernelDebugBreakPoint();
    }
    else if ( wAssertAction == JET_AssertStop )
    {
        for ( ; !g_fSkipAssert; )
        {
            Sleep( 100 );
        }
        g_fSkipAssert = fFalse;
    }
    else if ( wAssertAction == JET_AssertCrash )
    {
        RaiseFailFastExceptionCheckingSkipAssert();
    }
    else if ( JET_AssertFailFast == wAssertAction )
    {
        if ( !g_fSkipFailFast )
        {
            RaiseFailFastException();
        }
    }
    else if ( wAssertAction == JET_AssertMsgBox ||
              wAssertAction == JET_AssertSkippableMsgBox )
    {
        //  no message box on Linux; treat as JET_AssertBreak
        SetLastError( dwSavedGLE );
        UserDebugBreakPoint();
    }
    else if ( wAssertAction == JET_AssertSkipAll )
    {
        // Do nothing.
    }

    g_tidAssertFired = 0x0;
    LeaveCriticalSection( &g_csError );

    va_end( args );

    SetLastError( dwSavedGLE );
}


void AssertErr( const ERR err, PCSTR szFileName, const LONG lLine )
{
    DWORD dwSavedGLE = GetLastError();

    if ( JET_errSuccess == err )
    {
        FireWallAt( "UnexpectedAssertErrOnSuccess", szFileName, lLine );
    }
    else
    {
        FireWallAt( OSFormat( "AssertErr:%d", err ), szFileName, lLine );
    }

    SetLastError( dwSavedGLE );
}

#else  //  !DEBUG

extern ULONG_PTR UlParam( const INST* const pinst, const ULONG paramid );

void __stdcall AssertFail( PCSTR szMessageFormat, PCSTR szFilename, LONG lLine, ... )
{
    DWORD dwSavedGLE = GetLastError();

    CHAR szAssertText[ 500 ] = "VA FORMAT STRING FAIL";

    va_list args;
    va_start( args, lLine );

    if ( szMessageFormat )
    {
        OSStrCbVFormatA( szAssertText, sizeof( szAssertText ), szMessageFormat, args );
    }

    WCHAR wszIssueSource[ g_cchIssueSourceMax ] = L"FORMAT STRING FAIL";
    C_ASSERT( _countof( wszIssueSource ) < 260 );

    if ( FOSDllUp() )
    {
        ERRFormatIssueSource( wszIssueSource, sizeof( wszIssueSource ), dwSavedGLE, szFilename, lLine );
    }

    if ( !g_fNoWriteAssertEvent && FOSDllUp() )
    {
        WCHAR wszMessage[ _countof( szAssertText ) ] = L"FORMAT STRING FAIL";
        OSStrCbFormatW( wszMessage, sizeof( wszMessage ), L"%hs", szAssertText );

        const WCHAR * rgszT[] = { wszIssueSource, WszUtilImageBuildClass(), wszMessage };

        UtilReportEvent( eventInformation, GENERAL_CATEGORY, INTERNAL_TRACE_ID,
                         _countof( rgszT ), rgszT );
    }

    OSDiagTrackAssertFail( szAssertText, wszIssueSource );

    if ( FUtilSystemBetaFeatureEnabled_( nullptr, nullptr,
                                         (UtilSystemBetaSiteMode)UlParam( nullptr, JET_paramStageFlighting ),
                                         EseTestFeatures, L"EseFeatureTestOnly" ) )
    {
        EnterCriticalSection( &g_csError );
        g_tidAssertFired = DwUtilThreadId();

        switch ( g_wAssertAction )
        {
            case JET_AssertExit:
                _exit( ~0 );
                break;

            case JET_AssertFailFast:
                RaiseFailFastException();
                break;

            case JET_AssertBreak:
            case JET_AssertMsgBox:
            case JET_AssertSkippableMsgBox:
                SetLastError( dwSavedGLE );
                UserDebugBreakPoint();
                break;

            case JET_AssertStop:
                for ( ; !g_fSkipAssert ; )
                {
                    Sleep( 100 );
                }
                g_fSkipAssert = fFalse;
                break;

            case JET_AssertCrash:
                RaiseFailFastExceptionCheckingSkipAssert();
                break;

            case JET_AssertSkipAll:
                break;
        }

        g_tidAssertFired = 0x0;
        LeaveCriticalSection( &g_csError );
    }

    va_end( args );
    SetLastError( dwSavedGLE );
}

#endif  //  DEBUG


// ============================================================================================================
//  Enforces
// ============================================================================================================

VOID DefaultReportEnforceFailure( const WCHAR* wszContext, const CHAR* szMessage, const WCHAR* wszIssueSource )
{
    WCHAR wszMessage[ 1024 + 1 ] = L"FORMAT STRING FAIL";
    OSStrCbFormatW( wszMessage, sizeof( wszMessage ), L"%hs", szMessage ? szMessage : "" );
    const WCHAR * rgwszT[] = { wszIssueSource, WszUtilImageBuildClass(), wszMessage };

    UtilReportEvent( eventError, GENERAL_CATEGORY, ENFORCE_FAIL,
                     _countof( rgwszT ), rgwszT );

    OSDiagTrackEnforceFail( wszContext, szMessage, wszIssueSource );
}

BOOL g_fOverrideEnforceFailure = fFalse;
VOID (*g_pfnReportEnforceFailure)( const WCHAR* wszContext, const CHAR* szMessage, const WCHAR* wszIssueSource ) = DefaultReportEnforceFailure;

void (__stdcall *g_pfnEnforceContextFail)( const WCHAR* wszContext, const CHAR* szMessage, const CHAR* szFilename, LONG lLine ) = EnforceContextFail;

void __stdcall EnforceFail( const CHAR* szMessage, const CHAR* szFilename, LONG lLine )
{
    if ( g_pfnEnforceContextFail != nullptr )
    {
        g_pfnEnforceContextFail( nullptr, szMessage, szFilename, lLine );
    }
}

void __stdcall EnforceContextFail( const WCHAR* wszContext, const CHAR* szMessage, const CHAR* szFilename, LONG lLine )
{
    DWORD dwSavedGLE = GetLastError();

    EnterCriticalSection( &g_csError );

    g_fNoWriteAssertEvent = 1;

    if ( g_pfnReportEnforceFailure != nullptr )
    {
        WCHAR wszIssueSource[ g_cchIssueSourceMax ] = L"FORMAT STRING FAIL";
        C_ASSERT( _countof( wszIssueSource ) < 260 );

        ERRFormatIssueSource( wszIssueSource, sizeof( wszIssueSource ), dwSavedGLE, szFilename, lLine );

        g_pfnReportEnforceFailure( wszContext, szMessage, wszIssueSource );
    }

    SetLastError( dwSavedGLE );

    AssertTrackAt( fFalse, szMessage, szFilename, lLine );

    if ( !g_fOverrideEnforceFailure )
    {
        RaiseFailFastException();

        //  unreachable: belt-and-suspenders process kill
        _exit( ~0 );
    }

    LeaveCriticalSection( &g_csError );

    SetLastError( dwSavedGLE );
}


// ============================================================================================================
//  Exceptions
// ============================================================================================================
//
//  ENABLE_EXCEPTIONS is MSVC-only; on Linux the TRY/EXCEPT macros collapse to
//  if(1)/if(0) so _ExceptionFail / ExceptionDialog never get reached.
//  ExceptionId() is still referenced by signature; provide a stub.

const DWORD ExceptionId( EXCEPTION /* exception */ )
{
    return 0;
}


// ============================================================================================================
//  Error trap and ErrERRCheck
// ============================================================================================================

ERR g_errTrap = JET_errSuccess;

ERR ErrERRSetErrTrap( const ERR errSet )
{
    const ERR errRet = g_errTrap;
    g_errTrap = errSet;
    return errRet;
}

#ifdef DEBUG

ERR ErrERRCheck_( const ERR err, const CHAR* szFile, const LONG lLine )
{
    DWORD dwSavedGLE = GetLastError();

    AssertSz( ( ( err > -65536 && err < JET_errClientSpaceEnd ) ||
                ( err > JET_errClientSpaceBegin && err < (- JET_errClientSpaceBegin ) ) ||
                ( err > (- JET_errClientSpaceEnd ) && err < 65536 ) ),
              "Error value out of bounds." );

    if ( FOSRefTraceErrors() )
    {
        OSTraceWriteRefLog( ostrlSystemFixed, sysosrtlErrorThrow, (void*)(INT_PTR)err );
    }

    while ( g_tidAssertFired )
    {
        if ( g_tidAssertFired == DwUtilThreadId() )
        {
            return err;
        }
        UtilSleep( 1000 );
    }

    if ( err == g_errTrap )
    {
        FireWallAt( OSFormat( "ErrTrap:%d", g_errTrap ), szFile, lLine );
    }

    OSTrace( JET_tracetagErrors,
             OSFormat( "Error %d (0x%x) returned from %s@%d", err, err, szFile, lLine ) );

    switch ( err )
    {
        case JET_errSuccess:
            AssertSz( fFalse, "Shouldn't call ErrERRCheck() with JET_errSuccess." );
            break;

        case JET_errDerivedColumnCorruption:
            AssertSz( fFalse, "Corruption detected in column space of derived columns." );
            break;

        default:
            break;
    }

    PefLastThrow()->Set( szFile, lLine, err );

    SetLastError( dwSavedGLE );

    return err;
}

void ERRSetLastCall( _In_ const CHAR* szFile, _In_ const LONG lLine, _In_ const ERR err )
{
    Postls()->ulLineLastCall = lLine;
    Postls()->szFileLastCall = szFile;
    Postls()->errLastCall    = err;
}

#endif  //  DEBUG


// ============================================================================================================
//  Cleanup state TLS
// ============================================================================================================

#ifdef DEBUG

BOOL FOSSetCleanupState( const BOOL fInCleanupState )
{
    const BOOL fInCleanupStateSaved = Postls()->fCleanupState;
    Postls()->fCleanupState = fInCleanupState;
    return fInCleanupStateSaved;
}

inline BOOL FOSGetCleanupState()
{
    return Postls()->fCleanupState;
}

#endif  //  DEBUG


// ============================================================================================================
//  Test injection
// ============================================================================================================

#ifdef TEST_INJECTION

#include "_testinjection.hxx"

TESTINJECTION       g_rgTestInjections[g_cTestInjectionsMax];
LOCAL INT           g_cTestInjections;

ULONG               g_ulIDTrap = 0;

LOCAL CRITICAL_SECTION  g_csTestInjections;
LOCAL BOOL              g_fcsTestInjectionsInit;

ERR ErrEnableTestInjection( const ULONG ulID, const ULONG_PTR pv, const INT type, const ULONG ulProbability, const DWORD grbit )
{
    ERR err = JET_errSuccess;
    const JET_API_PTR pvT = pv;
    const JET_TESTINJECTIONTYPE typeT = (JET_TESTINJECTIONTYPE)type;
    TESTINJECTION injectionNew( ulID, pvT, ulProbability, grbit );

    Assert( g_fcsTestInjectionsInit );
    EnterCriticalSection( &g_csTestInjections );

    const BOOL fCleanup = ( grbit & JET_bitInjectionProbabilityCleanup ) != 0;

    if (    ( ulID == ulIDInvalid ) ||
            ( ( typeT < JET_TestInjectMin || typeT >= JET_TestInjectMax ) && !fCleanup ) )
    {
        Call( ErrERRCheck( JET_errInvalidParameter ) );
    }

    if (    ( grbit == JET_bitNil ) ||
            ( ( grbit & JET_bitInjectionProbabilityPct ) && ( grbit & JET_bitInjectionProbabilityCount ) ) ||
            ( !( grbit & JET_bitInjectionProbabilityCount ) &&
                ( ( grbit & JET_bitInjectionProbabilityPermanent ) || ( grbit & JET_bitInjectionProbabilityFailUntil ) ) ) ||
            ( ( grbit & JET_bitInjectionProbabilityPermanent ) && ( grbit & JET_bitInjectionProbabilityFailUntil ) ) ||
            ( fCleanup && ( grbit != JET_bitInjectionProbabilityCleanup ) ) )
    {
        Call( ErrERRCheck( JET_errInvalidParameter ) );
    }

    switch ( typeT )
    {
            default:
                Call( ErrERRCheck( JET_errTestInjectionNotSupported ) );

#ifdef  FAULT_INJECTION
            case JET_TestInjectFault:
#endif

#ifdef  CONFIGOVERRIDE_INJECTION
            case JET_TestInjectConfigOverride:
#endif

#ifdef  HANG_INJECTION
            case JET_TestInjectHang:
#endif

            break;
    }

    if ( g_cTestInjections >= _countof( g_rgTestInjections ) )
    {
        Call( ErrERRCheck( JET_errTooManyTestInjections ) );
    }

    TESTINJECTION* const pinjection = find( g_rgTestInjections,
                                            g_rgTestInjections + g_cTestInjections,
                                            injectionNew );

    const BOOL fNewInjection = ( pinjection == g_rgTestInjections + g_cTestInjections );

    if ( fCleanup && fNewInjection )
    {
        // no-op
    }
    else
    {
        if ( fCleanup && !fNewInjection )
        {
            pinjection->TraceStats();
            injectionNew.Disable();
        }

        *pinjection = injectionNew;

        if ( fNewInjection )
        {
            ++g_cTestInjections;
            Assert( g_cTestInjections <= _countof( g_rgTestInjections ) );
        }
    }

HandleError:
    LeaveCriticalSection( &g_csTestInjections );

    return err;
}

VOID RFSSuppressFaultInjection( const ULONG ulID )
{
    TESTINJECTION injectionTarget( ulID, 0, 0, 0 );

    Assert( g_fcsTestInjectionsInit );
    EnterCriticalSection( &g_csTestInjections );

    Assert( ulID != ulIDInvalid );

    TESTINJECTION* const pinjection = find( g_rgTestInjections,
                                            g_rgTestInjections + g_cTestInjections,
                                            injectionTarget );

    if ( pinjection && pinjection->Id() == ulID )
    {
        pinjection->Suppress();
    }

    LeaveCriticalSection( &g_csTestInjections );
}

VOID RFSUnsuppressFaultInjection( const ULONG ulID )
{
    TESTINJECTION injectionTarget( ulID, 0, 0, 0 );

    Assert( g_fcsTestInjectionsInit );
    EnterCriticalSection( &g_csTestInjections );

    Assert( ulID != ulIDInvalid );

    TESTINJECTION* const pinjection = find( g_rgTestInjections,
                                            g_rgTestInjections + g_cTestInjections,
                                            injectionTarget );

    if ( pinjection && pinjection->Id() == ulID )
    {
        pinjection->Unsuppress();
    }

    LeaveCriticalSection( &g_csTestInjections );
}


inline BOOL FRFSThreadEnabled();

INLINE TESTINJECTION* PinjectionFind_( const ULONG ulID )
{
    if ( 0 == g_cTestInjections )
    {
        return nullptr;
    }

    TESTINJECTION injectionSearch( ulID, 0, 0, 0x0 );

    TESTINJECTION* pinjection;
    const TESTINJECTION* const pinjectionTail = g_rgTestInjections + g_cTestInjections;
    if ( ( pinjection = find( g_rgTestInjections, (TESTINJECTION*)pinjectionTail, injectionSearch ) ) != pinjectionTail )
    {
        Assert( pinjection->Id() == ulID );
        return pinjection;
    }

    return nullptr;
}

INLINE BOOL FTestInjection_( const ULONG ulID, JET_API_PTR* const ppv )
{
    TESTINJECTION * const pinjection = PinjectionFind_( ulID );
    if ( pinjection )
    {
        if ( pinjection->FProbable() )
        {
            *ppv = pinjection->Pv();

            if ( g_ulIDTrap == pinjection->Id() || g_ulIDTrap == ulIDInvalid )
            {
                AssertSz( fFalse, "Test Injection Trap" );
            }

            return fTrue;
        }
    }

    return fFalse;
}

QWORD ChitsFaultInj( const ULONG ulID )
{
    TESTINJECTION * const pinjection = PinjectionFind_( ulID );
    if ( pinjection )
    {
        return pinjection->Chits();
    }

    return 0;
}

#else   //  !TEST_INJECTION

ERR ErrEnableTestInjection( const ULONG, const ULONG_PTR, const INT, const ULONG, const DWORD )
{
    return ErrERRCheck( JET_errTestInjectionNotSupported );
}

#endif  //  TEST_INJECTION


#ifdef FAULT_INJECTION

ERR ErrFaultInjection_( const ULONG ulID, const CHAR * const szFile, const LONG lLine )
{
    JET_API_PTR pv;

    if ( FTestInjection_( ulID, &pv ) )
    {
        return ( pv && ulID != 63560 ) ? ErrERRCheck_( (ERR)pv, szFile, lLine ) : (ERR)pv;
    }

    return JET_errSuccess;
}

#endif  //  FAULT_INJECTION

#ifdef CONFIGOVERRIDE_INJECTION

JET_API_PTR UlConfigOverrideInjection_( const ULONG ulID, const JET_API_PTR ulDefault )
{
    JET_API_PTR pv;

    if ( FTestInjection_( ulID, &pv ) )
    {
        return pv;
    }

    return ulDefault;
}

#endif  //  CONFIGOVERRIDE_INJECTION


#ifdef HANG_INJECTION

void HangInjection_( const ULONG ulID )
{
    JET_API_PTR pv;

    if ( FTestInjection_( ulID, &pv ) )
    {
        if ( bitHangInjectSleep & pv )
        {
            UtilSleep( ~mskHangInjectOptions & pv );
        }
    }
}

#endif  //  HANG_INJECTION


// ============================================================================================================
//  RFS2
// ============================================================================================================

const DWORD cRFSDisable             = (DWORD)-1;
const DWORD cRFSBreak               = (DWORD)-2;
const DWORD maskRFSThreadCountdown  = 0x80000000;

BOOL g_fDisableRFS      = fTrue;
BOOL g_fKnownRFSLeak    = fFalse;
BOOL g_fLogJETCall      = fFalse;
BOOL g_fLogRFS          = fFalse;
DWORD g_cRFSAlloc       = cRFSDisable;
DWORD g_cRFSIO          = cRFSBreak;

void EnableDisableRFS()
{
    if ( cRFSDisable == g_cRFSIO && cRFSDisable == g_cRFSAlloc )
    {
        g_fDisableRFS = fTrue;
    }
    else
    {
        g_fDisableRFS = fFalse;
    }
}

void COSLayerPreInit::SetRFSAlloc( ULONG cRFSAlloc )
{
    g_cRFSAlloc = cRFSAlloc;
    EnableDisableRFS();
}

void COSLayerPreInit::SetRFSIO( ULONG cRFSIO )
{
    g_cRFSIO = cRFSIO;
    EnableDisableRFS();
}

#ifdef RFS2

inline BOOL FRFSThreadEnabled()
{
    return ( ( Postls()->cRFSCountdown & maskRFSThreadCountdown ) == 0 );
}

inline LONG CRFSThreadCountdown()
{
    return ( Postls()->cRFSCountdown & ~maskRFSThreadCountdown );
}

inline void RFSThreadDecrementCountdown()
{
    Assert( !FRFSThreadEnabled() );
    Postls()->cRFSCountdown = ( ( CRFSThreadCountdown() - 1 ) | maskRFSThreadCountdown );
}

inline void RFSDecrementCount( LONG* const plTarget )
{
    volatile LONG lTarget = *plTarget;
    Assert( lTarget >= 0 );
    OSSYNC_FOREVER
    {
        if ( ( lTarget <= 0 ) || ( AtomicCompareExchange( plTarget, lTarget, lTarget - 1 ) == lTarget ) )
        {
            break;
        }
        lTarget = *plTarget;
    }
    Assert( lTarget >= 0 );
}

inline BOOL UtilRFSLog( const WCHAR* const wszType, const BOOL fPermitted )
{
    const WCHAR * rgszT[1];

    if ( !fPermitted )
    {
        g_fLogJETCall = fTrue;
    }

    if ( !g_fLogRFS && fPermitted )
    {
        return fPermitted;
    }

    rgszT[0] = wszType;

    UtilReportEvent( fPermitted ? eventInformation : eventWarning,
                     RFS2_CATEGORY,
                     fPermitted ? RFS2_PERMITTED_ID : RFS2_DENIED_ID,
                     1,
                     rgszT );

    return fPermitted;
}

BOOL UtilRFSAlloc( const WCHAR* const wszType, const INT Type )
{
    if ( FOSGetCleanupState() && Type == UnknownAllocResource )
    {
        AssertSz( fFalse, "Cleanup codepaths should not allocate resources." );
    }

    if ( g_fDisableRFS )
    {
        return UtilRFSLog( wszType, fTrue );
    }

    if ( ( ( cRFSBreak == g_cRFSAlloc && Type == 0 ) ||
           ( cRFSBreak == g_cRFSIO    && Type == 1 ) ) &&
         !g_fDisableRFS )
    {
        UserDebugBreakPoint();
    }

    DWORD* pcRFSGlobal = &g_cRFSAlloc;

    switch ( Type )
    {
        case 0:
            pcRFSGlobal = &g_cRFSAlloc;
            break;
        case 1:
            pcRFSGlobal = &g_cRFSIO;
            break;
        default:
            AssertSz( fFalse, "Unknown RFS type %u.", Type );
            break;
    }

    if ( ( *pcRFSGlobal == cRFSDisable ) || g_fDisableRFS )
    {
        return UtilRFSLog( wszType, fTrue );
    }
    if ( *pcRFSGlobal == 0 )
    {
        const BOOL fRFSThreadEnabled = FRFSThreadEnabled();
        const LONG cRFSThreadCountdown = CRFSThreadCountdown();
        if ( !fRFSThreadEnabled && ( cRFSThreadCountdown > 0 ) )
        {
            RFSThreadDecrementCountdown();
        }
        if ( fRFSThreadEnabled || ( cRFSThreadCountdown > 0 ) )
        {
            return UtilRFSLog( wszType, fFalse );
        }
        else
        {
            return UtilRFSLog( wszType, fTrue );
        }
    }
    else
    {
        RFSDecrementCount( (LONG*)pcRFSGlobal );
        return UtilRFSLog( wszType, fTrue );
    }
}

BOOL FRFSFailureDetected( const UINT Type )
{
    if ( g_fDisableRFS )
    {
        return fFalse;
    }
    if ( 0 == Type )
    {
        return ( 0 == g_cRFSAlloc );
    }
    else if ( 1 == Type )
    {
        return ( 0 == g_cRFSIO );
    }
    AssertSz( fFalse, "Unknown RFS type %u.", Type );
    return fFalse;
}

BOOL FRFSAnyFailureDetected()
{
    for ( UINT type = 0; type < RFSTypeMax; type++ )
    {
        if ( FRFSFailureDetected( type ) )
        {
            return fTrue;
        }
    }

    if ( FNegTest( fDiskIOError ) || FNegTest( fOutOfMemory ) )
    {
        return fTrue;
    }

    return fFalse;
}

void RFSSetKnownResourceLeak()
{
    g_fKnownRFSLeak = fTrue;
}

BOOL FRFSKnownResourceLeak()
{
    return g_fKnownRFSLeak;
}

LONG RFSThreadDisable( const LONG cRFSCountdown )
{
    const LONG cRFSCountdownOld = Postls()->cRFSCountdown;
    Postls()->cRFSCountdown = ( cRFSCountdown | maskRFSThreadCountdown );
    return cRFSCountdownOld;
}

void RFSThreadReEnable( const LONG cRFSCountdownOld )
{
    Assert( !FRFSThreadEnabled() );
    Postls()->cRFSCountdown = cRFSCountdownOld;
}

void UtilRFSLogJETCall( const CHAR* const szFunc, const ERR err, const CHAR* const szFile, const unsigned Line )
{
    WCHAR rgrgchT[2][16];
    WCHAR szFileName[ 260 ];
    WCHAR szFuncName[ 64 ];
    const WCHAR * rgszT[4];
    ERR errTemp;

    if ( err >= 0 || !g_fLogJETCall )
    {
        return;
    }

    errTemp = ErrOSStrCbFormatW( szFuncName, sizeof( szFuncName ), L"%hs", szFunc );
    Assert( JET_errSuccess <= errTemp || JET_errBufferTooSmall == errTemp );

    rgszT[0] = szFuncName;

    OSStrCbFormatW( rgrgchT[0], sizeof( rgrgchT[0] ), L"%d", err );
    rgszT[1] = rgrgchT[0];

    OSStrCbFormatW( szFileName, sizeof( szFileName ), L"%hs", szFile );
    rgszT[2] = szFileName;

    OSStrCbFormatW( rgrgchT[1], sizeof( rgrgchT[1] ), L"%d", Line );
    rgszT[3] = rgrgchT[1];

    UtilReportEvent( eventInformation, RFS2_CATEGORY, RFS2_JET_CALL_ID, 4, rgszT );
}

void UtilRFSLogJETErr( const ERR err, const CHAR* const szLabel, const CHAR* const szFile, const unsigned Line )
{
    WCHAR rgrgchT[2][16];
    WCHAR szFileName[ 260 ];
    WCHAR szLabelName[ 64 ];
    const WCHAR * rgszT[4];

    if ( !g_fLogJETCall )
    {
        return;
    }

    OSStrCbFormatW( rgrgchT[0], sizeof( rgrgchT[0] ), L"%d", err );
    rgszT[0] = rgrgchT[0];

    OSStrCbFormatW( szLabelName, sizeof( szLabelName ), L"%hs", szLabel );
    rgszT[1] = szLabelName;

    OSStrCbFormatW( szFileName, sizeof( szFileName ), L"%hs", szFile );
    rgszT[2] = szFileName;

    OSStrCbFormatW( rgrgchT[1], sizeof( rgrgchT[1] ), L"%d", Line );
    rgszT[3] = rgrgchT[1];

    UtilReportEvent( eventInformation, PERFORMANCE_CATEGORY, RFS2_JET_ERROR_ID, 4, rgszT );
}

BOOL RFSError::Check( ERR err, ... ) const
{
    va_list arg_ptr;
    va_start( arg_ptr, err );

    for ( ; err != 0; err = va_arg( arg_ptr, ERR ) )
    {
        Assert( err > -9000 && err < 9000 );
        if ( m_err == err )
        {
            break;
        }
    }

    va_end( arg_ptr );
    return ( err != 0 );
}

#else   // !RFS2

inline BOOL FRFSThreadEnabled()
{
    return fFalse;
}

#endif  // RFS2


// ============================================================================================================
//  Lifecycle
// ============================================================================================================

void OSErrorPostterm()
{
#ifdef TEST_INJECTION
    if ( g_fcsTestInjectionsInit )
    {
        DeleteCriticalSection( &g_csTestInjections );
        g_fcsTestInjectionsInit = fFalse;
    }
    for ( TESTINJECTION* pinjection = g_rgTestInjections;
          pinjection < ( g_rgTestInjections + g_cTestInjections );
          pinjection++ )
    {
        pinjection->TraceStats();
    }
    g_cTestInjections = 0;
#endif

    if ( g_fCritSecErrorInit )
    {
        DeleteCriticalSection( &g_csError );
        g_fCritSecErrorInit = fFalse;
    }
}

BOOL FOSErrorPreinit()
{
    if ( !InitializeCriticalSectionAndSpinCount( &g_csError, 0 ) )
    {
        goto HandleError;
    }
    g_fCritSecErrorInit = fTrue;

#ifdef TEST_INJECTION
    if ( !InitializeCriticalSectionAndSpinCount( &g_csTestInjections, 0 ) )
    {
        goto HandleError;
    }
    g_fcsTestInjectionsInit = fTrue;
#endif

    return fTrue;

HandleError:
    OSErrorPostterm();
    return fFalse;
}

void OSErrorTerm()
{
    //  nop
}

ERR ErrOSErrorInit()
{
    return JET_errSuccess;
}


#ifndef RTM
DWORD g_grbitNegativeTesting = 0x0;
#endif


// ============================================================================================================
//  Repair / Integrity Utility
// ============================================================================================================

BOOL FUtilRepairIntegrityMsgBox( const WCHAR * const /* wszMsg */ )
{
    //  no GUI on Linux; default to "Cancel" (i.e. do not run the destructive
    //  recovery operation without explicit confirmation).
    return fFalse;
}


// ============================================================================================================
//  Win32 -> JET error mapping
// ============================================================================================================

ERR ErrOSErrFromWin32Err( _In_ DWORD dwWinError, _In_ ERR errDefault )
{
    switch ( dwWinError )
    {
        case NO_ERROR:
            return JET_errSuccess;

        case ERROR_DISK_FULL:
            return ErrERRCheck( JET_errDiskFull );

        case ERROR_HANDLE_EOF:
        case ERROR_VC_DISCONNECTED:
        case ERROR_IO_DEVICE:
        case ERROR_DEVICE_NOT_CONNECTED:
            return ErrERRCheck( JET_errDiskIO );

        case ERROR_FILE_CORRUPT:
        case ERROR_DISK_CORRUPT:
            return ErrERRCheck( JET_errFileSystemCorruption );

        case ERROR_NOT_READY:
        case ERROR_NO_MORE_FILES:
        case ERROR_FILE_NOT_FOUND:
            return ErrERRCheck( JET_errFileNotFound );

        case ERROR_PATH_NOT_FOUND:
        case ERROR_DIRECTORY:
        case ERROR_BAD_NET_NAME:
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_PATHNAME:
        case ERROR_INVALID_NAME:
            return ErrERRCheck( JET_errInvalidPath );

        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_WRITE_PROTECT:
            return ErrERRCheck( JET_errFileAccessDenied );

        case ERROR_TOO_MANY_OPEN_FILES:
            return ErrERRCheck( JET_errOutOfFileHandles );

        case ERROR_NO_SYSTEM_RESOURCES:
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_WORKING_SET_QUOTA:
            return ErrERRCheck( JET_errOutOfMemory );

        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return ErrERRCheck( JET_errFileAlreadyExists );

        default:
            return ErrERRCheck( errDefault );
    }
}

ERR ErrOSErrFromWin32Err( _In_ DWORD dwWinError )
{
    return ErrOSErrFromWin32Err( dwWinError, JET_errInternalError );
}


// ============================================================================================================
//  Source-file basename helper
// ============================================================================================================

const CHAR * g_szBadSourceFileName = "#BadFileName#";

const CHAR * SzSourceFileName( const CHAR * szFilePath )
{
    if ( nullptr == szFilePath || szFilePath[0] == '\0' )
    {
        return "";
    }
    if ( nullptr == strrchr( szFilePath, chPathDelimiter ) )
    {
        return szFilePath;
    }
    if ( strrchr( szFilePath, chPathDelimiter ) + sizeof( CHAR ) >= szFilePath + strlen( szFilePath ) )
    {
        ExpectedSz( fFalse, "Source Code Path with delimiter at very end of string." );
        return g_szBadSourceFileName;
    }

    return strrchr( szFilePath, chPathDelimiter ) + sizeof( CHAR );
}

VOID OSErrorPrintLastError( const WCHAR * const szMessage )
{
    const DWORD dwGLE = GetLastError();
    if ( szMessage )
    {
        for ( const WCHAR* p = szMessage; *p; p++ )
        {
            fputc( (unsigned char)( *p & 0xFF ), stderr );
        }
    }
    fprintf( stderr, " (%u)\n", dwGLE );
}
