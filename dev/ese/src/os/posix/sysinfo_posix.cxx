// POSIX equivalent of sysinfo.cxx. Wraps the Windows-shaped surface
// declared in published/inc/os/sysinfo.hxx with Linux-native sources of
// truth: getpid + readlink("/proc/self/exe") for process attributes,
// sysconf(_SC_NPROCESSORS_ONLN) for processor count, uname() for system
// version, dl_iterate_phdr/dladdr for the loaded image, and __builtin_*
// CPU intrinsics for SSE/SSE2/POPCNT/AVX detection.
//
// Items that have no Linux equivalent (Win8 packaged-process detection,
// Wow64, registry-driven version overrides, the WNF downgrade-window
// query, and Software Licensing config queries) all return safe defaults
// so that beta-feature staging behaves predictably for the v1 port.

#include "osstd.hxx"

#include <cpuid.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

extern void OSIProcessAbort();

extern volatile BOOL g_fDllUp;

namespace
{
const size_t cwchPath = 1024;

WCHAR g_wszProcessPath[cwchPath];
WCHAR g_wszProcessName[cwchPath];
WCHAR g_wszProcessFileName[cwchPath];
WCHAR g_wszProcessFriendlyName[cwchPath];
WCHAR g_wszImagePath[cwchPath];
WCHAR g_wszImageName[cwchPath];
WCHAR g_wszImageBuildClass[cwchPath];

void NarrowToWide(const char* sz, WCHAR* wsz, size_t cwch)
{
    if (cwch == 0)
        return;
    size_t i = 0;
    for (; sz && sz[i] != '\0' && i + 1 < cwch; ++i)
    {
        wsz[i] = (WCHAR) (unsigned char) sz[i];
    }
    wsz[i] = L'\0';
}

void SplitTrailingComponent(const WCHAR* path, WCHAR* base, size_t cwchBase, WCHAR* ext, size_t cwchExt)
{
    if (cwchBase)
        base[0] = L'\0';
    if (cwchExt)
        ext[0] = L'\0';

    if (!path)
        return;

    size_t cwch = 0;
    while (path[cwch] != L'\0')
        cwch++;

    size_t iLastSlash = cwch;
    for (size_t i = 0; i < cwch; ++i)
    {
        if (path[i] == L'/' || path[i] == L'\\')
        {
            iLastSlash = i;
        }
    }
    const WCHAR* leaf = (iLastSlash == cwch)
                        ? path
                        : (path + iLastSlash + 1);

    size_t cwchLeaf = 0;
    while (leaf[cwchLeaf] != L'\0')
        cwchLeaf++;

    size_t iLastDot = cwchLeaf;
    for (size_t i = 0; i < cwchLeaf; ++i)
    {
        if (leaf[i] == L'.')
        {
            iLastDot = i;
        }
    }

    size_t cwchStem = (iLastDot == cwchLeaf)
                      ? cwchLeaf
                      : iLastDot;
    if (cwchBase)
    {
        size_t n = cwchStem < cwchBase - 1
                   ? cwchStem
                   : cwchBase - 1;
        for (size_t i = 0; i < n; ++i)
            base[i] = leaf[i];
        base[n] = L'\0';
    }

    if (cwchExt && iLastDot != cwchLeaf)
    {
        size_t cwchExtIn = cwchLeaf - iLastDot;
        size_t n = cwchExtIn < cwchExt - 1
                   ? cwchExtIn
                   : cwchExt - 1;
        for (size_t i = 0; i < n; ++i)
            ext[i] = leaf[iLastDot + i];
        ext[n] = L'\0';
    }
}
} // anonymous

//
//  Process Attributes
//

const WCHAR* WszUtilProcessName() { return g_wszProcessName; }
const WCHAR* WszUtilProcessFileName() { return g_wszProcessFileName; }
const WCHAR* WszUtilProcessFriendlyName() { return g_wszProcessFriendlyName; }
const WCHAR* WszUtilProcessPath() { return g_wszProcessPath; }

LOCAL DWORD g_dwProcessId;

const DWORD DwUtilProcessId()
{
    return g_dwProcessId;
}

const DWORD CUtilProcessProcessor()
{
    return OSSyncGetProcessorCountMax();
}

volatile BOOL g_fProcessAbort = fFalse;

const BOOL FUtilProcessAbort()
{
    return g_fProcessAbort;
}

//
//  System Attributes
//

LOCAL DWORD g_dwSystemVersionMajor;
LOCAL DWORD g_dwSystemVersionMinor;
LOCAL DWORD g_dwSystemBuildNumber;
LOCAL DWORD g_dwSystemServicePackNumber;

DWORD DwUtilSystemVersionMajor() { return g_dwSystemVersionMajor; }
DWORD DwUtilSystemVersionMinor() { return g_dwSystemVersionMinor; }
DWORD DwUtilSystemBuildNumber() { return g_dwSystemBuildNumber; }
DWORD DwUtilSystemServicePackNumber() { return g_dwSystemServicePackNumber; }

#ifdef DEBUG
INT g_eSpeedOfAcLinePowerLoss = 0;
#endif

LOCAL BOOL g_fRestrictIdleActivity = fFalse;

BOOL FUtilSystemRestrictIdleActivity()
{
    // Linux does not surface the AC-line power transition the way the
    // Win32 GetSystemPowerStatus path did. Honor the override the user
    // sets explicitly via COSLayerPreInit and otherwise allow idle work.
    BOOL fLastResult = g_fRestrictIdleActivity
                       ? fTrue
                       : fFalse;
    fLastResult = (BOOL) UlConfigOverrideInjection(56368, fLastResult);
    return fLastResult;
}

//
//  Image Attributes
//

VOID* g_pvImageBaseAddress;

const VOID* PvUtilImageBaseAddress()
{
    return g_pvImageBaseAddress;
}

const WCHAR* WszUtilImageName() { return g_wszImageName; }
const WCHAR* WszUtilImagePath() { return g_wszImagePath; }
const WCHAR* WszUtilImageVersionName() { return WSZVERSIONNAME; }

LOCAL DWORD g_dwImageVersionMajor;
LOCAL DWORD g_dwImageVersionMinor;
LOCAL DWORD g_dwImageBuildNumberMajor;
LOCAL DWORD g_dwImageBuildNumberMinor;

DWORD DwUtilImageVersionMajor() { return g_dwImageVersionMajor; }
DWORD DwUtilImageVersionMinor() { return g_dwImageVersionMinor; }
DWORD DwUtilImageBuildNumberMajor() { return g_dwImageBuildNumberMajor; }
DWORD DwUtilImageBuildNumberMinor() { return g_dwImageBuildNumberMinor; }

const WCHAR* WszUtilImageBuildClass() { return g_wszImageBuildClass; }

//
//  Signal handler equivalent of the Win32 ^C / ^Break console handler.
//

static struct sigaction g_oldSigInt;
static struct sigaction g_oldSigTerm;
static BOOL g_fSignalHandlerInstalled = fFalse;

static void SysinfoSignalHandler(int /* sig */)
{
    OSIProcessAbort();
}

//
//  CPU feature detection.
//

LOCAL BOOL g_fSSEInstructionsAvailable;
LOCAL BOOL g_fSSE2InstructionsAvailable;
LOCAL BOOL g_fPopcntAvailable;
LOCAL BOOL g_fAVXEnabled;

BOOL FSSEInstructionsAvailable() { return g_fSSEInstructionsAvailable; }
BOOL FSSE2InstructionsAvailable() { return g_fSSE2InstructionsAvailable; }
BOOL FPopcntAvailable() { return g_fPopcntAvailable; }
BOOL FAVXEnabled() { return g_fAVXEnabled; }

LOCAL VOID DetermineProcessorCapabilities()
{
#if defined( ESE_ARCH_AMD64 ) || defined( ESE_ARCH_X86 )
    // SSE/SSE2 are baseline on x86_64. Verify via cpuid for completeness.
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
    {
        g_fSSEInstructionsAvailable = (edx & (1u << 25)) != 0;
        g_fSSE2InstructionsAvailable = (edx & (1u << 26)) != 0;
        g_fPopcntAvailable = (ecx & (1u << 23)) != 0;

        const bool fOSXSAVE = (ecx & (1u << 27)) != 0;
        const bool fAVX = (ecx & (1u << 28)) != 0;
        if (fOSXSAVE && fAVX)
        {
            unsigned int xcr0_lo = 0, xcr0_hi = 0;
            __asm__ __volatile__ ( "xgetbv" : "=a"(xcr0_lo), "=d"(xcr0_hi) : "c"(0) );
            const QWORD xcr0 = ((QWORD) xcr0_hi << 32) | xcr0_lo;
            g_fAVXEnabled = ((xcr0 & 0x6) == 0x6);
        }
    }
#else
    g_fSSEInstructionsAvailable = fFalse;
    g_fSSE2InstructionsAvailable = fFalse;
    g_fPopcntAvailable = fFalse;
    g_fAVXEnabled = fFalse;
#endif
}

//
//  Sysinfo subsystem lifecycle.
//

void OSSysinfoPostterm()
{
    if (g_fSignalHandlerInstalled)
    {
        sigaction(SIGINT, &g_oldSigInt, nullptr);
        sigaction(SIGTERM, &g_oldSigTerm, nullptr);
        g_fSignalHandlerInstalled = fFalse;
    }
}

LOCAL BOOL FGetSystemVersion()
{
    struct utsname un;
    if (uname(&un) != 0)
    {
        g_dwSystemVersionMajor = 0;
        g_dwSystemVersionMinor = 0;
        g_dwSystemBuildNumber = 0;
        g_dwSystemServicePackNumber = 0;
        return fFalse;
    }

    DWORD major = 0, minor = 0, micro = 0;
    const char* p = un.release;
    while (*p >= '0' && *p <= '9')
    {
        major = major * 10 + (DWORD) (*p - '0');
        p++;
    }
    if (*p == '.')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            minor = minor * 10 + (DWORD) (*p - '0');
            p++;
        }
    }
    if (*p == '.')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            micro = micro * 10 + (DWORD) (*p - '0');
            p++;
        }
    }

    g_dwSystemVersionMajor = major;
    g_dwSystemVersionMinor = minor;
    g_dwSystemBuildNumber = micro;
    g_dwSystemServicePackNumber = 0;
    return fTrue;
}

LOCAL VOID DetermineProcessPath()
{
    char szBuf[cwchPath] = {0};
    ssize_t cb = readlink("/proc/self/exe", szBuf, sizeof(szBuf) - 1);
    if (cb < 0)
        cb = 0;
    szBuf[cb] = '\0';

    NarrowToWide(szBuf, g_wszProcessPath, _countof(g_wszProcessPath));

    WCHAR wszExt[256] = {0};
    SplitTrailingComponent(g_wszProcessPath, g_wszProcessName, _countof(g_wszProcessName), wszExt, _countof(wszExt));

    OSStrCbFormatW(g_wszProcessFileName, sizeof(g_wszProcessFileName),
        L"%ws%ws", g_wszProcessName, wszExt);

    OSStrCbCopyW(g_wszProcessFriendlyName, sizeof( g_wszProcessFriendlyName ), g_wszProcessName);
}

LOCAL VOID DetermineImagePath()
{
    Dl_info info = {nullptr};
    if (dladdr((void*) &DetermineImagePath, &info) && info.dli_fname)
    {
        NarrowToWide(info.dli_fname, g_wszImagePath, _countof(g_wszImagePath));
        g_pvImageBaseAddress = info.dli_fbase;
    }
    else
    {
        OSStrCbCopyW(g_wszImagePath, sizeof( g_wszImagePath ), g_wszProcessPath);
        g_pvImageBaseAddress = nullptr;
    }

    SplitTrailingComponent(g_wszImagePath, g_wszImageName, _countof(g_wszImageName), nullptr, 0);
}

BOOL FOSSysinfoPreinit()
{
    DetermineProcessPath();

    g_dwProcessId = (DWORD) getpid();

    FGetSystemVersion();

    DetermineImagePath();

    g_dwImageVersionMajor = atoi(PRODUCT_MAJOR);
    g_dwImageVersionMinor = atoi(PRODUCT_MINOR);
    g_dwImageBuildNumberMajor = atoi(BUILD_MAJOR);
    g_dwImageBuildNumberMinor = atoi(BUILD_MINOR);

    WCHAR wszBuf[256];
    OSStrCbFormatW(wszBuf, sizeof(wszBuf),
        L"%ws[%02I32u.%02I32u.%04I32u.%03I32u]",
        WSZVERSIONNAME,
        DwUtilImageVersionMajor(), DwUtilImageVersionMinor(),
        DwUtilImageBuildNumberMajor(), DwUtilImageBuildNumberMinor());
#ifdef DEBUG
    OSStrCbAppendW(wszBuf, sizeof( wszBuf ), L" DEBUG");
#else
    OSStrCbAppendW(wszBuf, sizeof( wszBuf ), L" RETAIL");
#endif
#ifdef RTM
    OSStrCbAppendW(wszBuf, sizeof( wszBuf ), L" RTM");
#endif
    OSStrCbAppendW(wszBuf, sizeof( wszBuf ), L" ASCII");

    OSStrCbCopyW(g_wszImageBuildClass, sizeof( g_wszImageBuildClass ), wszBuf);

    DetermineProcessorCapabilities();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SysinfoSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, &g_oldSigInt) == 0 &&
        sigaction(SIGTERM, &sa, &g_oldSigTerm) == 0)
    {
        g_fSignalHandlerInstalled = fTrue;
    }

    return fTrue;
}

VOID COSLayerPreInit::SetProcessFriendlyName(const WCHAR* const wszProcessFriendlyNameNew)
{
    const WCHAR* wsz = wszProcessFriendlyNameNew;
    if (wsz == nullptr || LOSStrLengthW(wsz) == 0)
    {
        wsz = WszUtilProcessName();
    }
    OSStrCbCopyW(g_wszProcessFriendlyName, sizeof( g_wszProcessFriendlyName ), wsz);
}

VOID COSLayerPreInit::SetRestrictIdleActivity(const BOOL fRestrictIdleActivity)
{
    g_fRestrictIdleActivity = fRestrictIdleActivity;
}

COSEventTraceIdCheck g_traceidcheckSysGlobal;

void OSSysTraceStationId(const DWORD /* TraceStationIdentificationReason */ tsidr_)
{
    const TraceStationIdentificationReason tsidr = (TraceStationIdentificationReason) tsidr_;
    if (!g_traceidcheckSysGlobal.FAnnounceTime<_etguidSysStationId>(tsidr))
    {
        return;
    }
    ETSysStationId(tsidr,
        DwUtilImageVersionMajor(), DwUtilImageVersionMinor(),
        DwUtilImageBuildNumberMajor(), DwUtilImageBuildNumberMinor(),
        WszUtilProcessFileName());
}

BOOL FOSSetupRunning()
{
    // Linux has no equivalent of HKLM\System\Setup; ESE never participates
    // in OS install on this platform.
    return fFalse;
}

const BOOL FUtilProcessIsPackaged()
{
    return fFalse;
}

const BOOL FUtilIProcessIsWow64()
{
    return fFalse;
}

//
//  Beta-feature staging table (portable, kept verbatim).
//

enum UtilSystemBetaSiteMode : ULONG
{
    usbsmTestEnvExplicit = 0x01,
    usbsmTestEnvLocalMode = 0x04,
    usbsmTestEnvAlphaMode = 0x10,
    usbsmTestEnvBetaMode = 0x40,
    usbsmTestEnvDebugMode = 0x80,
    usbsmTestEnvAll = 0xD5,

    usbsmSelfhostExplicit = 0x0100,
    usbsmSelfhostLocalMode = 0x0400,
    usbsmSelfhostAlphaMode = 0x1000,
    usbsmSelfhostBetaMode = 0x4000,
    usbsmSelfhostAll = 0x5500,

    usbsmProdExplicit = 0x010000,
    usbsmProdLocalMode = 0x040000,
    usbsmProdAlphaMode = 0x100000,
    usbsmProdBetaMode = 0x400000,
    usbsmProdWindows = 0x800000,
    usbsmProdAll = 0xD50000,

    usbsmExFeatRiskyFeatTest = 0x00000002,
    usbsmExFeatNegTest = 0x00000008,
    usbsmExFeatOtherFeatTest = 0x00000020,

    usbsmExFeatNext1 = 0x01000000,
    usbsmExFeatNext2 = 0x02000000,
    usbsmExFeatNext3 = 0x04000000,
    usbsmExFeatNext4 = 0x08000000,
    usbsmExFeatNext5 = 0x10000000,
    usbsmExFeatNext6 = 0x20000000,
    usbsmExFeatNext7 = 0x40000000,
    usbsmExFeatNext8 = 0x80000000,

    usbsmExFeatLast,
    usbsmExFeatureMask = 0xFF00002A,
};

C_ASSERT(sizeof( UtilSystemBetaSiteMode ) == sizeof( ULONG ));

DEFINE_ENUM_FLAG_OPERATORS_BASIC(UtilSystemBetaSiteMode);

UtilSystemBetaSiteMode usbsmPrimaryEnvironments = (usbsmTestEnvAll | usbsmSelfhostAll | usbsmProdAll);
UtilSystemBetaSiteMode usbsmExFeatures = usbsmExFeatureMask;
#ifdef DEBUG
UtilSystemBetaSiteMode usbsmExFeatureMin = usbsmExFeatRiskyFeatTest;
UtilSystemBetaSiteMode usbsmExFeatureMax = usbsmExFeatLast;
#endif

C_ASSERT(JET_bitStageTestEnvLocalMode == usbsmTestEnvLocalMode);
C_ASSERT(JET_bitStageTestEnvAlphaMode == usbsmTestEnvAlphaMode);
C_ASSERT(JET_bitStageTestEnvBetaMode == usbsmTestEnvBetaMode);

C_ASSERT(JET_bitStageSelfhostLocalMode == usbsmSelfhostLocalMode);
C_ASSERT(JET_bitStageSelfhostAlphaMode == usbsmSelfhostAlphaMode);
C_ASSERT(JET_bitStageSelfhostBetaMode == usbsmSelfhostBetaMode);

C_ASSERT(JET_bitStageProdLocalMode == usbsmProdLocalMode);
C_ASSERT(JET_bitStageProdAlphaMode == usbsmProdAlphaMode);
C_ASSERT(JET_bitStageProdBetaMode == usbsmProdBetaMode);

const LONG fLoggedEventAlready = -1;
C_ASSERT(fLoggedEventAlready);

UtilSystemBetaConfig g_rgbetaconfigs[] =
{
    {fTrue, fFeatureDynamic, EseTestFeatures, usbsmTestEnvAll},

#ifndef OS_LAYER_VIOLATIONS
    {
        fTrue, fFeatureDynamic, EseTestCase,
        usbsmTestEnvAll OnDebug(| usbsmSelfhostAlphaMode) OnNonRTM(| usbsmSelfhostBetaMode) | usbsmExFeatOtherFeatTest
    },
    {fFalse, fFeatureStatic, EseTestCaseTwo, usbsmTestEnvAll},
    {fFalse, fFeatureDynamic, EseTestCaseThree, usbsmProdAll},
    {fFalse, fFeatureDynamic, EseRiskyFeatTest, usbsmSelfhostAlphaMode | usbsmExFeatRiskyFeatTest},
#endif
};
C_ASSERT(_countof( g_rgbetaconfigs ) == EseFeatureMax);

void UtilReportBetaFeatureInUse(const INST* const pinst, const UtilSystemBetaSiteMode usbsmCurrent,
    const ULONG featureid, PCWSTR const wszFeatureName)
{
    if (fFalse == AtomicCompareExchange(&(g_rgbetaconfigs[featureid].fSuppressInfoEvent), (LONG) fFalse,
        (LONG) fLoggedEventAlready))
    {
        OSTrace(JET_tracetagVersionAndStagingChecks,
            OSFormat( "Beta Feature %d staging value %d (usbsmCurrent = %d).\n", featureid, fTrue, usbsmCurrent ));
    }
}

ERR ErrUtilSystemSlConfiguration(
    _In_z_ const WCHAR* pszValueName,
    _Out_ DWORD* pdwValue)
{
    Assert(pdwValue);
    UNREFERENCED_PARAMETER(pszValueName);

    ERR err = ErrERRCheck(JET_errDisabledFunctionality);

#ifdef DEBUG
    err = (ERR) UlConfigOverrideInjection(43276, (ULONG_PTR)err);
    if (err)
    {
        ErrERRCheck(err);
    }
#endif

    if (err >= JET_errSuccess)
    {
        *pdwValue = (DWORD) UlConfigOverrideInjection(55564, *pdwValue);
    }

    return err;
}

ERR ErrUtilOsDowngradeWindowExpired(_Out_ BOOL* pfExpired)
{
    // No equivalent of WNF_DEP_UNINSTALL_DISABLED on Linux. Treat the
    // downgrade window as already expired so non-revertable behavior is
    // the default, matching the test/prod fault-injection default of 3.
    const ERR errFaultInj = (ERR) UlConfigOverrideInjection(50026, 3);
    if (errFaultInj < JET_errSuccess)
    {
        return ErrERRCheck(errFaultInj);
    }

    *pfExpired = fTrue;
    *pfExpired = (BOOL) UlConfigOverrideInjection(50026, *pfExpired);
    return JET_errSuccess;
}

void OSSysinfoTerm()
{
    OSSysTraceStationId(tsidrCloseTerm);

    for (ULONG featureid = 0; featureid < _countof(g_rgbetaconfigs); featureid++)
    {
        Assert(g_rgbetaconfigs[ featureid ].fStaticFeature == fFeatureDynamic ||
            g_rgbetaconfigs[ featureid ].fStaticFeature == fFeatureStatic);
        if (g_rgbetaconfigs[featureid].fSuppressInfoEvent == fLoggedEventAlready)
        {
            (void) AtomicExchange(&(g_rgbetaconfigs[featureid].fSuppressInfoEvent), fFalse);
        }
    }
}

ERR ErrOSSysinfoInit()
{
    OSSysTraceStationId(tsidrOpenInit);
    return JET_errSuccess;
}
