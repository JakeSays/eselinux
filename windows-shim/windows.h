// Linux shim for <windows.h>. Brings in the Win32 type surface and
// declares the API entry points the OS internal headers reference at
// parse time. Implementations live in os/posix/.
#pragma once

#include <winnt.h>
#include <winioctl.h>
#include <winerror.h>

#include <alloca.h>
// glibc's <alloca.h> only exposes the __builtin_alloca macro under __GNUC__,
// which clang does not define under -fms-compatibility. Without the macro,
// every alloca() call would emit an external function reference. Force the
// builtin so _malloca expands to a stack alloc as Win32 callers expect.
#undef alloca
#define alloca( size ) __builtin_alloca( ( size ) )
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <sys/random.h>

// Engine + test code uses bare `min(a, b)` and `max(a, b)` with mismatched
// argument types — this matches osstd_.hxx's pattern where heterogeneous
// overloads sit alongside `using namespace std;`. Tests that include
// windows.h (most of devlibtest) need the same overloads to compile, so
// publish them once here. C++ only.
//
// osstd_.hxx (in os/posix/) defines identical overloads. Use the same
// guard macro so whichever header lands first wins; the other becomes
// a no-op.
#if defined(__cplusplus) && !defined(_ESE_HETEROGENEOUS_MINMAX_DEFINED)
#define _ESE_HETEROGENEOUS_MINMAX_DEFINED 1
#include <algorithm>
#include <type_traits>
template < class A, class B,
           class = std::enable_if_t< !std::is_same_v< A, B > > >
constexpr auto min( A a, B b ) -> std::common_type_t< A, B >
{
    using T = std::common_type_t< A, B >;
    return T( a ) < T( b ) ? T( a ) : T( b );
}
template < class A, class B,
           class = std::enable_if_t< !std::is_same_v< A, B > > >
constexpr auto max( A a, B b ) -> std::common_type_t< A, B >
{
    using T = std::common_type_t< A, B >;
    return T( a ) > T( b ) ? T( a ) : T( b );
}
using std::min;
using std::max;
#endif // __cplusplus && !_ESE_HETEROGENEOUS_MINMAX_DEFINED

#ifdef __cplusplus
extern "C" {
#endif

//  Last-error machinery. Linux implementation maintains a thread-local
//  Win32-style error code; ErrOSErrFromWin32Err / ErrOSFileIFromWinError
//  consume it. Posix-side translates errno → Win32 codes at API boundaries.
//
DWORD GetLastError( void );
void  SetLastError( DWORD dwErrCode );

//  Process / thread identity. Linux implementations live in
//  os/posix/thread_posix.cxx; pid_t / thread-id mapping uses gettid()
//  / getpid() so the values are reportable in /proc and ps output.
//
DWORD GetCurrentThreadId( void );
DWORD GetCurrentProcessId( void );
HANDLE GetCurrentThread( void );
HANDLE GetCurrentProcess( void );

//  Thread-handle API. Engine call sites use OpenThread( SYNCHRONIZE, ... )
//  to test thread liveness via WaitForSingleObject. Posix layer backs the
//  HANDLE with an opaque struct that wraps a pthread/tid pair.
//
HANDLE OpenThread( DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId );
BOOL   CloseHandle( HANDLE hObject );

//  Wait API. WaitForSingleObject return values:
//     WAIT_OBJECT_0  — signalled
//     WAIT_TIMEOUT   — dwMilliseconds elapsed
//     WAIT_ABANDONED — owning thread exited (mutexes only)
//     WAIT_FAILED    — error; GetLastError() has detail
//
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0       ((DWORD)0x00000000L)
#define WAIT_ABANDONED      ((DWORD)0x00000080L)
#define WAIT_TIMEOUT        ((DWORD)0x00000102L)
#define WAIT_FAILED         ((DWORD)0xFFFFFFFFL)
#define INFINITE            0xFFFFFFFF
#endif

DWORD WaitForSingleObject(   HANDLE hHandle, DWORD dwMilliseconds );
DWORD WaitForSingleObjectEx( HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable );
DWORD WaitForMultipleObjects(   DWORD nCount, const HANDLE* lpHandles,
                                BOOL bWaitAll, DWORD dwMilliseconds );
DWORD WaitForMultipleObjectsEx( DWORD nCount, const HANDLE* lpHandles,
                                BOOL bWaitAll, DWORD dwMilliseconds, BOOL bAlertable );

//  Synchronization primitives. Posix layer wraps:
//    Event     → eventfd / pthread_cond
//    Mutex     → pthread_mutex
//    Semaphore → POSIX sem_t
//  All exposed through HANDLE so engine call sites bind unchanged.
//
HANDLE CreateEventW(   LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
                       BOOL bInitialState, LPCWSTR lpName );
HANDLE CreateEventA(   LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
                       BOOL bInitialState, LPCSTR  lpName );
#ifdef _UNICODE
#define CreateEvent CreateEventW
#else
#define CreateEvent CreateEventA
#endif

// QueueUserWorkItem — spawns a callback on a worker thread. Used by
// devlibtest's resmgrunit. WT_EXECUTEDEFAULT is the default flag (0); the
// other WT_* flags select stack size / I/O bindings, none of which apply
// to a POSIX detached pthread.
#define WT_EXECUTEDEFAULT       0x00000000
#define WT_EXECUTELONGFUNCTION  0x00000010
// (LPTHREAD_START_ROUTINE is declared further down where CreateThread lives.)
HANDLE OpenEventW(     DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName );
BOOL   SetEvent(       HANDLE hEvent );
BOOL   ResetEvent(     HANDLE hEvent );
BOOL   PulseEvent(     HANDLE hEvent );

HANDLE CreateMutexW(   LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner,
                       LPCWSTR lpName );
HANDLE OpenMutexW(     DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName );
BOOL   ReleaseMutex(   HANDLE hMutex );

HANDLE CreateSemaphoreW( LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount,
                         LONG lMaximumCount, LPCWSTR lpName );
HANDLE CreateSemaphoreExW( LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount,
                           LONG lMaximumCount, LPCWSTR lpName,
                           DWORD dwFlags, DWORD dwDesiredAccess );
BOOL   ReleaseSemaphore( HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount );

// Win32 access-mask constants. POSIX semaphores have no concept of an
// access mask; the winapi_sync.cxx CreateSemaphore* implementations
// accept and ignore the value. Defined for source compatibility (the
// devlibtest semaphoreperf suite passes SEMAPHORE_ALL_ACCESS).
#ifndef SEMAPHORE_ALL_ACCESS
#define SEMAPHORE_ALL_ACCESS    0x1F0003
#define SEMAPHORE_MODIFY_STATE  0x0002
#endif

//  Critical sections. Posix layer wraps a pthread_mutex (PTHREAD_MUTEX_
//  RECURSIVE) inside the opaque RTL_CRITICAL_SECTION struct in winnt.h.
//  SpinCount is honored by adaptive spinning before sleeping.
//
void  InitializeCriticalSection(             LPCRITICAL_SECTION pcs );
BOOL  InitializeCriticalSectionAndSpinCount( LPCRITICAL_SECTION pcs, DWORD dwSpinCount );
void  EnterCriticalSection(                  LPCRITICAL_SECTION pcs );
BOOL  TryEnterCriticalSection(               LPCRITICAL_SECTION pcs );
void  LeaveCriticalSection(                  LPCRITICAL_SECTION pcs );
void  DeleteCriticalSection(                 LPCRITICAL_SECTION pcs );
DWORD SetCriticalSectionSpinCount(           LPCRITICAL_SECTION pcs, DWORD dwSpinCount );

//  Thread Local Storage. Posix layer maps to pthread_key_create /
//  pthread_getspecific / pthread_setspecific.
//
#ifndef TLS_OUT_OF_INDEXES
#define TLS_OUT_OF_INDEXES  ((DWORD)0xFFFFFFFF)
#endif

DWORD TlsAlloc(    void );
BOOL  TlsFree(     DWORD dwTlsIndex );
PVOID TlsGetValue( DWORD dwTlsIndex );
BOOL  TlsSetValue( DWORD dwTlsIndex, PVOID lpTlsValue );

//  Thread management. Posix layer wraps pthread_* under HANDLE.
//
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)( LPVOID lpThreadParameter );

//  CreateThread flags. CREATE_SUSPENDED defers the call to the thread
//  proc until ResumeThread() — posix layer mirrors this with a
//  pthread_cond barrier. STACK_SIZE_PARAM_IS_A_RESERVATION is honored
//  via pthread_attr_setstacksize.
//
#ifndef CREATE_SUSPENDED
#define CREATE_SUSPENDED                        0x00000004
#define STACK_SIZE_PARAM_IS_A_RESERVATION       0x00010000
#endif

#ifndef THREAD_PRIORITY_NORMAL
#define THREAD_PRIORITY_IDLE            (-15)
#define THREAD_PRIORITY_LOWEST          (-2)
#define THREAD_PRIORITY_BELOW_NORMAL    (-1)
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_ABOVE_NORMAL    1
#define THREAD_PRIORITY_HIGHEST         2
#define THREAD_PRIORITY_TIME_CRITICAL   15
#define THREAD_PRIORITY_ERROR_RETURN    INT_MAX
#define THREAD_MODE_BACKGROUND_BEGIN    0x00010000
#define THREAD_MODE_BACKGROUND_END      0x00020000
#endif

//  Thread access rights (winnt.h on Windows). OpenThread( rights, ... )
//  filters at the kernel level; on Linux these are advisory.
//
#ifndef THREAD_TERMINATE
#define THREAD_TERMINATE                0x0001
#define THREAD_SUSPEND_RESUME            0x0002
#define THREAD_GET_CONTEXT               0x0008
#define THREAD_SET_CONTEXT               0x0010
#define THREAD_QUERY_INFORMATION         0x0040
#define THREAD_SET_INFORMATION           0x0020
#define THREAD_SET_THREAD_TOKEN          0x0080
#define THREAD_IMPERSONATE               0x0100
#define THREAD_DIRECT_IMPERSONATION      0x0200
#define THREAD_SET_LIMITED_INFORMATION   0x0400
#define THREAD_QUERY_LIMITED_INFORMATION 0x0800
#define THREAD_ALL_ACCESS                0x1FFFFF
#endif

#ifndef DUPLICATE_SAME_ACCESS
#define DUPLICATE_CLOSE_SOURCE  0x00000001
#define DUPLICATE_SAME_ACCESS   0x00000002
#endif

HANDLE CreateThread( LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize,
                     LPTHREAD_START_ROUTINE lpStartAddress, LPVOID lpParameter,
                     DWORD dwCreationFlags, LPDWORD lpThreadId );
BOOL   QueueUserWorkItem( LPTHREAD_START_ROUTINE Function, PVOID Context, ULONG Flags );
DWORD  ResumeThread(      HANDLE hThread );
DWORD  SuspendThread(     HANDLE hThread );
BOOL   TerminateThread(   HANDLE hThread, DWORD dwExitCode );
BOOL   GetExitCodeThread( HANDLE hThread, LPDWORD lpExitCode );
BOOL   SetThreadPriority(      HANDLE hThread, int nPriority );
int    GetThreadPriority(      HANDLE hThread );
BOOL   SetThreadPriorityBoost( HANDLE hThread, BOOL bDisablePriorityBoost );
BOOL   GetThreadPriorityBoost( HANDLE hThread, PBOOL pDisablePriorityBoost );
HANDLE GetCurrentThread(  void );
HANDLE GetCurrentProcess( void );

//  Sleep / yield. Posix layer maps to nanosleep / sched_yield.
//
void  Sleep(  DWORD dwMilliseconds );
DWORD SleepEx( DWORD dwMilliseconds, BOOL bAlertable );
void  SwitchToThread( void );

//  Process / thread identity. Posix layer maps to getpid() / gettid().
//
DWORD GetCurrentProcessId( void );

//  Wall-clock and broken-down time. Posix layer derives from
//  clock_gettime(CLOCK_REALTIME) plus localtime_r / gmtime_r.
//
void GetSystemTime(            LPSYSTEMTIME lpSystemTime );
void GetLocalTime(             LPSYSTEMTIME lpSystemTime );
void GetSystemTimeAsFileTime(  LPFILETIME   lpSystemTimeAsFileTime );
BOOL FileTimeToSystemTime(     const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime );
BOOL SystemTimeToFileTime(     const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime );
BOOL FileTimeToLocalFileTime(  const FILETIME* lpFileTime, LPFILETIME lpLocalFileTime );
BOOL LocalFileTimeToFileTime(  const FILETIME* lpLocalFileTime, LPFILETIME lpFileTime );

//  TIME_ZONE_INFORMATION pointer is opaque to the engine — call sites
//  pass NULL (use current system zone). Only the NULL path is implemented.
//
typedef struct _TIME_ZONE_INFORMATION TIME_ZONE_INFORMATION, *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;
BOOL SystemTimeToTzSpecificLocalTime( const TIME_ZONE_INFORMATION* lpTimeZone,
                                      const SYSTEMTIME*            lpUniversalTime,
                                      LPSYSTEMTIME                 lpLocalTime );

//  Monotonic milliseconds since boot. Posix layer reads
//  clock_gettime(CLOCK_MONOTONIC) and folds to 32-bit ms.
//
DWORD     GetTickCount(   void );
ULONGLONG GetTickCount64( void );

//  Processor feature query. Win32 surface; on Linux x86_64 the engine
//  only asks about RDTSC, which is always available. The posix layer
//  returns TRUE for PF_RDTSC_INSTRUCTION_AVAILABLE and FALSE otherwise.
//
#define PF_RDTSC_INSTRUCTION_AVAILABLE 8
BOOL IsProcessorFeaturePresent( DWORD ProcessorFeature );

//  High-resolution counter. Posix layer maps to clock_gettime(CLOCK_MONOTONIC)
//  in nanoseconds; Frequency is hard-coded 10000000 (100-ns ticks).
//
BOOL QueryPerformanceFrequency( LARGE_INTEGER* lpFrequency );
BOOL QueryPerformanceCounter(   LARGE_INTEGER* lpPerformanceCount );

//  System paths. Engine call sites read GetSystemWindowsDirectoryW() to
//  locate global resource files; on Linux the posix layer returns a
//  build-time prefix (e.g. /usr/share/ese).
//
UINT GetSystemWindowsDirectoryW( LPWSTR lpBuffer, UINT uSize );
UINT GetWindowsDirectoryW(       LPWSTR lpBuffer, UINT uSize );
UINT GetSystemDirectoryW(        LPWSTR lpBuffer, UINT uSize );

//  Debug output. On Windows attaches to the debugger via OutputDebugString;
//  posix layer routes to stderr (and, when LTTng lands, to a tracepoint).
//
void OutputDebugStringA( LPCSTR  lpOutputString );
void OutputDebugStringW( LPCWSTR lpOutputString );
#if defined(UNICODE) || defined(_UNICODE)
#define OutputDebugString OutputDebugStringW
#else
#define OutputDebugString OutputDebugStringA
#endif

void DebugBreak( void );
BOOL IsDebuggerPresent( void );

// RaiseFailFastException — Win32 fast-fail. Engine error path & some
// devlibtest fixtures call this to abort the process. On Linux there is
// no SEH, so the implementation just aborts.
struct _EXCEPTION_RECORD;
struct _CONTEXT;
[[noreturn]] void RaiseFailFastException( struct _EXCEPTION_RECORD* pExceptionRecord,
                                          struct _CONTEXT*          pContextRecord,
                                          DWORD                     dwFlags );

// DEBUG_EVENT / LPDEBUG_EVENT — referenced only by os/norm.cxx's
// WaitForDebugEventEx feature-detection (the function is never invoked
// for real on the Linux port; see winapi_debug.cxx).  Layout is unused
// at runtime; just enough to make the prototype declaration parse.
typedef struct _DEBUG_EVENT
{
    DWORD dwDebugEventCode;
    DWORD dwProcessId;
    DWORD dwThreadId;
    BYTE  u[ 168 ];   /* opaque DEBUG_EVENT_UNION payload */
} DEBUG_EVENT, *LPDEBUG_EVENT;

BOOL DuplicateHandle( HANDLE hSourceProcessHandle, HANDLE hSourceHandle,
                      HANDLE hTargetProcessHandle, LPHANDLE lpTargetHandle,
                      DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwOptions );

//  HANDLE flag manipulation. PROTECT_FROM_CLOSE prevents CloseHandle()
//  from succeeding; INHERIT lets child processes inherit the handle.
//
#ifndef HANDLE_FLAG_INHERIT
#define HANDLE_FLAG_INHERIT             0x00000001
#define HANDLE_FLAG_PROTECT_FROM_CLOSE  0x00000002
#endif

BOOL GetHandleInformation( HANDLE hObject, LPDWORD lpdwFlags );
BOOL SetHandleInformation( HANDLE hObject, DWORD   dwMask, DWORD dwFlags );

//  Local heap. Windows distinguishes Local/Global/Heap allocators; the
//  engine uses LocalAlloc for a few small structures (REF_TRACE_LOG,
//  norm error messages). LMEM_ZEROINIT is the only flag the engine
//  passes so the posix shim wraps malloc + memset.
//
#ifndef LMEM_FIXED
#define LMEM_FIXED          0x0000
#define LMEM_MOVEABLE       0x0002
#define LMEM_ZEROINIT       0x0040
#define LPTR                ( LMEM_FIXED | LMEM_ZEROINIT )
#define LHND                ( LMEM_MOVEABLE | LMEM_ZEROINIT )
#endif

HLOCAL LocalAlloc( UINT uFlags, size_t uBytes );
HLOCAL LocalFree(  HLOCAL hMem );

//  Memory bulk-ops (winbase.h on Windows; macros over RtlZeroMemory etc).
//
#ifndef ZeroMemory
#define ZeroMemory( dst, sz )       memset( (dst), 0,    (sz) )
#define FillMemory( dst, sz, fill ) memset( (dst), (fill), (sz) )
#define CopyMemory( dst, src, sz )  memcpy( (dst), (src), (sz) )
#define MoveMemory( dst, src, sz )  memmove( (dst), (src), (sz) )
#endif

//  Stack-walking. Engine uses this to record call stacks in the ref-
//  trace ring buffers. Posix layer wraps glibc backtrace().
//
USHORT RtlCaptureStackBackTrace( DWORD FramesToSkip, DWORD FramesToCapture,
                                 PVOID* BackTrace, PDWORD BackTraceHash );

//  File API. Engine call sites pass wide paths (UTF-16); the posix layer
//  converts to UTF-8 and calls open()/read()/write()/etc. HANDLE is an
//  opaque pointer to a struct with the underlying fd plus state needed
//  for OVERLAPPED / scatter-gather emulation.
//
HANDLE CreateFileW( LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile );
BOOL   ReadFile(    HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
                    LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped );
BOOL   WriteFile(   HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                    LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped );
BOOL   ReadFileScatter( HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
                        DWORD nNumberOfBytesToRead, LPDWORD lpReserved, LPOVERLAPPED lpOverlapped );
BOOL   WriteFileGather( HANDLE hFile, FILE_SEGMENT_ELEMENT aSegmentArray[],
                        DWORD nNumberOfBytesToWrite, LPDWORD lpReserved, LPOVERLAPPED lpOverlapped );
BOOL   FlushFileBuffers( HANDLE hFile );
BOOL   GetFileSizeEx( HANDLE hFile, PLARGE_INTEGER lpFileSize );
BOOL   SetFilePointerEx( HANDLE hFile, LARGE_INTEGER liDistanceToMove,
                         PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod );
BOOL   SetEndOfFile( HANDLE hFile );
BOOL   SetFileValidData( HANDLE hFile, LONGLONG ValidDataLength );
BOOL   GetFileInformationByHandleEx( HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                                     LPVOID lpFileInformation, DWORD dwBufferSize );
BOOL   SetFileInformationByHandle(   HANDLE hFile, FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
                                     LPVOID lpFileInformation, DWORD dwBufferSize );
BOOL   DeleteFileW( LPCWSTR lpFileName );
BOOL   MoveFileW(   LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName );
BOOL   MoveFileExW( LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags );
BOOL   CopyFileW(   LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists );

//  MoveFileEx flags (winbase.h on Windows). The engine passes
//  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH for atomic rename
//  with metadata sync; on Linux rename(2) is already atomic, and
//  fdatasync of the parent directory is the analogue we apply on top.
//
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING       0x00000001
#define MOVEFILE_COPY_ALLOWED           0x00000002
#define MOVEFILE_DELAY_UNTIL_REBOOT     0x00000004
#define MOVEFILE_WRITE_THROUGH          0x00000008
#define MOVEFILE_CREATE_HARDLINK        0x00000010
#define MOVEFILE_FAIL_IF_NOT_TRACKABLE  0x00000020
#endif

//  File-system enumeration & attributes.
//
DWORD  GetFileAttributesW( LPCWSTR lpFileName );
BOOL   SetFileAttributesW( LPCWSTR lpFileName, DWORD dwFileAttributes );
BOOL   CreateDirectoryW( LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes );
BOOL   RemoveDirectoryW( LPCWSTR lpPathName );
DWORD  GetFullPathNameW( LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR* lpFilePart );
DWORD  GetCurrentDirectoryW( DWORD nBufferLength, LPWSTR lpBuffer );
DWORD  GetTempPathW( DWORD nBufferLength, LPWSTR lpBuffer );
UINT   GetTempFileNameW( LPCWSTR lpPathName, LPCWSTR lpPrefixString, UINT uUnique, LPWSTR lpTempFileName );

//  Volume / drive APIs.
//
UINT   GetDriveTypeW( LPCWSTR lpRootPathName );
BOOL   GetVolumeInformationW( LPCWSTR lpRootPathName, LPWSTR lpVolumeNameBuffer,
                              DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
                              LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                              LPWSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize );
BOOL   GetVolumePathNameW( LPCWSTR lpszFileName, LPWSTR lpszVolumePathName, DWORD cchBufferLength );
BOOL   GetVolumeNameForVolumeMountPointW( LPCWSTR lpszVolumeMountPoint, LPWSTR lpszVolumeName,
                                          DWORD cchBufferLength );
BOOL   GetDiskFreeSpaceW( LPCWSTR lpRootPathName, LPDWORD lpSectorsPerCluster,
                          LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                          LPDWORD lpTotalNumberOfClusters );
BOOL   GetDiskFreeSpaceExW( LPCWSTR lpDirectoryName, PULARGE_INTEGER lpFreeBytesAvailable,
                            PULARGE_INTEGER lpTotalNumberOfBytes, PULARGE_INTEGER lpTotalNumberOfFreeBytes );

//  Find-file APIs. WIN32_FIND_DATAW shape matches Windows so the engine
//  reads dwFileAttributes etc directly off the struct.
//
typedef struct _WIN32_FIND_DATAW {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    dwReserved0;
    DWORD    dwReserved1;
    WCHAR    cFileName[260];
    WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATAW, *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;

HANDLE FindFirstFileW( LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData );
BOOL   FindNextFileW(  HANDLE hFindFile,    LPWIN32_FIND_DATAW lpFindFileData );
BOOL   FindClose( HANDLE hFindFile );

//  Volume enumeration. Engine uses these to walk volumes for crash-
//  recovery / file-id resolution; posix layer enumerates /proc/mounts
//  or /sys/block to synthesize volume names.
//
HANDLE FindFirstVolumeW( LPWSTR lpszVolumeName, DWORD cchBufferLength );
BOOL   FindNextVolumeW(  HANDLE hFindVolume, LPWSTR lpszVolumeName, DWORD cchBufferLength );
BOOL   FindVolumeClose(  HANDLE hFindVolume );
BOOL   GetVolumePathNamesForVolumeNameW( LPCWSTR lpszVolumeName, LPWSTR lpszVolumePathNames,
                                         DWORD cchBufferLength, PDWORD lpcchReturnLength );
DWORD  QueryDosDeviceW( LPCWSTR lpDeviceName, LPWSTR lpTargetPath, DWORD ucchMax );

//  GetFinalPathNameByHandle flags (winbase.h on Windows). Engine uses
//  VOLUME_NAME_DOS (e.g. "C:\Foo\bar.edb") and VOLUME_NAME_NONE (e.g.
//  "\Foo\bar.edb"). FILE_NAME_OPENED returns the path used at open
//  time; FILE_NAME_NORMALIZED resolves symlinks.
//
#ifndef VOLUME_NAME_DOS
#define VOLUME_NAME_DOS         0x0
#define VOLUME_NAME_GUID        0x1
#define VOLUME_NAME_NT          0x2
#define VOLUME_NAME_NONE        0x4
#define FILE_NAME_NORMALIZED    0x0
#define FILE_NAME_OPENED        0x8
#endif

DWORD GetFinalPathNameByHandleW( HANDLE hFile, LPWSTR lpszFilePath, DWORD cchFilePath, DWORD dwFlags );

//  OpenFileById — engine uses this to follow stable file IDs (NTFS /
//  ReFS) past path renames. Posix layer maps to openat() with the
//  cached parent fd.
//
HANDLE OpenFileById( HANDLE hVolumeHint, LPFILE_ID_DESCRIPTOR lpFileId, DWORD dwDesiredAccess,
                     DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                     DWORD dwFlagsAndAttributes );

//  LOCALE_NAME_* constants and the entire winnls.h surface now come
//  from libnls's public header (included at the end of this file).
//  The shim no longer defines them locally.

//  Code-page identifiers (winnls.h on Windows). The engine uses CP_ACP
//  (ANSI codepage of the OS) and CP_UTF8 for Ascii<->Unicode conversion;
//  posix layer maps these to iconv() / mbsrtowcs() with UTF-8.
//
#ifndef CP_ACP
#define CP_ACP                  0
#define CP_OEMCP                1
#define CP_MACCP                2
#define CP_THREAD_ACP           3
#define CP_SYMBOL               42
#define CP_UTF7                 65000
#define CP_UTF8                 65001
#endif

#ifndef MB_ERR_INVALID_CHARS
#define MB_PRECOMPOSED          0x00000001
#define MB_COMPOSITE            0x00000002
#define MB_USEGLYPHCHARS        0x00000004
#define MB_ERR_INVALID_CHARS    0x00000008
#endif

#ifndef WC_ERR_INVALID_CHARS
#define WC_COMPOSITECHECK       0x00000200
#define WC_DISCARDNS            0x00000010
#define WC_SEPCHARS             0x00000020
#define WC_DEFAULTCHAR          0x00000040
#define WC_NO_BEST_FIT_CHARS    0x00000400
#define WC_ERR_INVALID_CHARS    0x00000080
#endif

int MultiByteToWideChar( UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr,
                         int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar );
int WideCharToMultiByte( UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr,
                         int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte,
                         LPCSTR lpDefaultChar, LPBOOL lpUsedDefaultChar );

//  GetDateFormatW / GetTimeFormatW. Engine uses these only for
//  diagnostic output, so the posix layer always formats with a fixed
//  en-US-style pattern based on the flag.
//
//  LCMAP_*, NORM_*, NLSVERSIONINFO, CSTR_*, all the LCMapStringEx /
//  CompareStringEx prototypes — every one of those is declared by
//  libnls's <winnls.h>, included at the end of this file.  Date/time
//  formatting (GetDateFormatW / GetTimeFormatW + the DATE_* / TIME_*
//  flags) isn't in libnls's surface yet; ESE doesn't call them today,
//  so leaving as a TODO.
//
//  The LCID aggregate constants below (LANG_USER_DEFAULT, LOCALE_NEUTRAL,
//  ...) are MAKELCID compositions built from the LANG_* / SUBLANG_*
//  pieces defined earlier in this shim — they're winnt.h-realm in
//  Win32, not winnls.h, so libnls doesn't carry them.
#ifndef LANG_USER_DEFAULT
#define LANG_USER_DEFAULT          ( MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ) )
#define LANG_SYSTEM_DEFAULT        ( MAKELANGID( LANG_NEUTRAL, SUBLANG_SYS_DEFAULT ) )
#endif
#ifndef LOCALE_USER_DEFAULT
#define LOCALE_USER_DEFAULT        ( MAKELCID( LANG_USER_DEFAULT,   SORT_DEFAULT ) )
#define LOCALE_SYSTEM_DEFAULT      ( MAKELCID( LANG_SYSTEM_DEFAULT, SORT_DEFAULT ) )
#define LOCALE_NEUTRAL             ( MAKELCID( MAKELANGID( LANG_NEUTRAL,   SUBLANG_NEUTRAL ), SORT_DEFAULT ) )
#define LOCALE_INVARIANT           ( MAKELCID( MAKELANGID( LANG_INVARIANT, SUBLANG_NEUTRAL ), SORT_DEFAULT ) )
#endif

//  DeviceIoControl. Used by the disk layer for ATA / NVMe pass-through;
//  the posix layer routes IOCTL_STORAGE_QUERY_PROPERTY etc to ioctl()
//  on /dev/sd? or returns synthesized descriptors.
//
BOOL DeviceIoControl( HANDLE hDevice, DWORD dwIoControlCode, LPVOID lpInBuffer, DWORD nInBufferSize,
                      LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned,
                      LPOVERLAPPED lpOverlapped );

//  Range-lock APIs. The engine relies on these for cooperative file
//  locking; posix layer uses fcntl(F_OFD_SETLK) under the hood.
//
#ifndef LOCKFILE_FAIL_IMMEDIATELY
#define LOCKFILE_FAIL_IMMEDIATELY   0x00000001
#define LOCKFILE_EXCLUSIVE_LOCK     0x00000002
#endif

BOOL LockFileEx(   HANDLE hFile, DWORD dwFlags, DWORD dwReserved,
                   DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh,
                   LPOVERLAPPED lpOverlapped );
BOOL UnlockFileEx( HANDLE hFile, DWORD dwReserved,
                   DWORD nNumberOfBytesToUnlockLow, DWORD nNumberOfBytesToUnlockHigh,
                   LPOVERLAPPED lpOverlapped );

//  GetFileInformationByHandle returns the BY_HANDLE_FILE_INFORMATION
//  layout the engine reads directly.
//
typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD    dwVolumeSerialNumber;
    DWORD    nFileSizeHigh;
    DWORD    nFileSizeLow;
    DWORD    nNumberOfLinks;
    DWORD    nFileIndexHigh;
    DWORD    nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *PBY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;

BOOL GetFileInformationByHandle( HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation );

//  Win32 thread-pool API. Backed by a Linux-native worker pool in
//  os/posix/threadpool_posix.cxx; the engine sees opaque TP_* handles.
//  TP_CALLBACK_ENVIRON is a value-type the engine zero-initializes and
//  passes by pointer to InitializeThreadpoolEnvironment etc; we keep it
//  large enough to match the Windows layout the engine reserves space
//  for. The posix layer doesn't read these fields directly.
//
typedef struct _TP_POOL              TP_POOL,              *PTP_POOL;
typedef struct _TP_WORK              TP_WORK,              *PTP_WORK;
typedef struct _TP_TIMER             TP_TIMER,             *PTP_TIMER;
typedef struct _TP_WAIT              TP_WAIT,              *PTP_WAIT;
typedef struct _TP_IO                TP_IO,                *PTP_IO;
typedef struct _TP_CLEANUP_GROUP     TP_CLEANUP_GROUP,     *PTP_CLEANUP_GROUP;
typedef struct _TP_CALLBACK_INSTANCE TP_CALLBACK_INSTANCE, *PTP_CALLBACK_INSTANCE;

typedef struct _TP_CALLBACK_ENVIRON_V3 {
    DWORD     Version;
    PTP_POOL  Pool;
    PTP_CLEANUP_GROUP CleanupGroup;
    void    (*CleanupGroupCancelCallback)( PVOID, PVOID );
    PVOID     RaceDll;
    void*     ActivationContext;
    void    (*FinalizationCallback)( PTP_CALLBACK_INSTANCE, PVOID );
    union {
        DWORD Flags;
        struct {
            DWORD LongFunction:1;
            DWORD Persistent:1;
            DWORD Private:30;
        } s;
    } u;
    int       CallbackPriority;
    DWORD     Size;
} TP_CALLBACK_ENVIRON, *PTP_CALLBACK_ENVIRON;

typedef void (NTAPI *PTP_SIMPLE_CALLBACK)( PTP_CALLBACK_INSTANCE Instance, PVOID Context );
typedef void (NTAPI *PTP_WORK_CALLBACK)(   PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work );
typedef void (NTAPI *PTP_TIMER_CALLBACK)(  PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_TIMER Timer );
typedef void (NTAPI *PTP_WAIT_CALLBACK)(   PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WAIT Wait, DWORD WaitResult );

//  Thread-pool entry points. Implementations live in
//  os/posix/threadpool_posix.cxx (a thin wrapper over a Linux worker
//  pool — pthreads + condvar + work queue per pool).
//
PTP_POOL CreateThreadpool( PVOID reserved );
void     CloseThreadpool( PTP_POOL ptpp );
BOOL     SetThreadpoolThreadMinimum( PTP_POOL ptpp, DWORD cthrdMic );
void     SetThreadpoolThreadMaximum( PTP_POOL ptpp, DWORD cthrdMost );

PTP_CLEANUP_GROUP CreateThreadpoolCleanupGroup( void );
void              CloseThreadpoolCleanupGroup( PTP_CLEANUP_GROUP ptpcg );
void              CloseThreadpoolCleanupGroupMembers( PTP_CLEANUP_GROUP ptpcg, BOOL fCancelPendingCallbacks, PVOID pvCleanupContext );

void InitializeThreadpoolEnvironment( PTP_CALLBACK_ENVIRON pcbe );
void DestroyThreadpoolEnvironment(    PTP_CALLBACK_ENVIRON pcbe );
void SetThreadpoolCallbackPool(         PTP_CALLBACK_ENVIRON pcbe, PTP_POOL ptpp );
void SetThreadpoolCallbackCleanupGroup( PTP_CALLBACK_ENVIRON pcbe, PTP_CLEANUP_GROUP ptpcg,
                                        void (*pfng)( PVOID, PVOID ) );

PTP_WORK CreateThreadpoolWork( PTP_WORK_CALLBACK pfnwk, PVOID pv, PTP_CALLBACK_ENVIRON pcbe );
void     SubmitThreadpoolWork( PTP_WORK pwk );
BOOL     TrySubmitThreadpoolCallback( PTP_SIMPLE_CALLBACK pfns, PVOID pv, PTP_CALLBACK_ENVIRON pcbe );
void     WaitForThreadpoolWorkCallbacks( PTP_WORK pwk, BOOL fCancelPendingCallbacks );
void     CloseThreadpoolWork( PTP_WORK pwk );

PTP_TIMER CreateThreadpoolTimer( PTP_TIMER_CALLBACK pfnti, PVOID pv, PTP_CALLBACK_ENVIRON pcbe );
void      SetThreadpoolTimer( PTP_TIMER pti, PFILETIME pftDueTime, DWORD msPeriod, DWORD msWindowLength );
void      WaitForThreadpoolTimerCallbacks( PTP_TIMER pti, BOOL fCancelPendingCallbacks );
void      CloseThreadpoolTimer( PTP_TIMER pti );

PTP_WAIT CreateThreadpoolWait( PTP_WAIT_CALLBACK pfnwa, PVOID pv, PTP_CALLBACK_ENVIRON pcbe );
void     SetThreadpoolWait( PTP_WAIT pwa, HANDLE h, PFILETIME pftTimeout );
void     WaitForThreadpoolWaitCallbacks( PTP_WAIT pwa, BOOL fCancelPendingCallbacks );
void     CloseThreadpoolWait( PTP_WAIT pwa );

//  FormatMessageW (winbase.h on Windows). Engine uses this only to look
//  up message-resource strings from its own image; on Linux there is no
//  embedded message table, so the posix layer always returns 0 and the
//  engine's call sites fall through to the "no message" branch.
//
#ifndef FORMAT_MESSAGE_ALLOCATE_BUFFER
#define FORMAT_MESSAGE_ALLOCATE_BUFFER  0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS   0x00000200
#define FORMAT_MESSAGE_FROM_STRING      0x00000400
#define FORMAT_MESSAGE_FROM_HMODULE     0x00000800
#define FORMAT_MESSAGE_FROM_SYSTEM      0x00001000
#define FORMAT_MESSAGE_ARGUMENT_ARRAY   0x00002000
#define FORMAT_MESSAGE_MAX_WIDTH_MASK   0x000000FF
#endif

#ifndef MAKELANGID
#define MAKELANGID( p, s )      ( ( ( ( WORD )( s ) ) << 10 ) | ( WORD )( p ) )
#define LANG_NEUTRAL            0x00
#define LANG_INVARIANT          0x7f
#define LANG_ENGLISH            0x09
#define SUBLANG_NEUTRAL         0x00
#define SUBLANG_DEFAULT         0x01
#define SUBLANG_SYS_DEFAULT     0x02
#define SUBLANG_ENGLISH_US      0x01
#define SUBLANG_DEFAULT         0x01
#define SORT_DEFAULT            0x0
#define MAKELCID( lgid, srtid ) ( ( DWORD )( ( ( ( DWORD )( ( WORD )( srtid ) ) ) << 16 ) | ( ( DWORD )( ( WORD )( lgid ) ) ) ) )
#define LANGIDFROMLCID( lcid )  ( ( WORD )( lcid ) )
#endif

DWORD FormatMessageW( DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId,
                      LPWSTR lpBuffer, DWORD nSize, va_list* Arguments );

//  Console width queries. Engine call sites (cprintf) use these only
//  to size diagnostic output to the terminal width; the posix layer
//  routes to ioctl(TIOCGWINSZ).
//
#ifndef STD_OUTPUT_HANDLE
#define STD_INPUT_HANDLE     ((DWORD)-10)
#define STD_OUTPUT_HANDLE    ((DWORD)-11)
#define STD_ERROR_HANDLE     ((DWORD)-12)
#endif

typedef struct _COORD {
    SHORT X;
    SHORT Y;
} COORD, *PCOORD;

typedef struct _SMALL_RECT {
    SHORT Left;
    SHORT Top;
    SHORT Right;
    SHORT Bottom;
} SMALL_RECT, *PSMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
    COORD      dwSize;
    COORD      dwCursorPosition;
    WORD       wAttributes;
    SMALL_RECT srWindow;
    COORD      dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO, *PCONSOLE_SCREEN_BUFFER_INFO;

HANDLE GetStdHandle( DWORD nStdHandle );
BOOL   GetConsoleScreenBufferInfo( HANDLE hConsoleOutput, PCONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo );

#ifdef __cplusplus
}  // extern "C"
#endif

//  errno_t / rand_s — MSVC secure CRT idioms used in the engine. The
//  posix layer maps rand_s to getrandom() so callers see the same return
//  contract (0 == success, non-zero == errno).
//
#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

static inline errno_t rand_s( unsigned int* pui )
{
    if ( !pui )
        return EINVAL;
    ssize_t n = getrandom( pui, sizeof( *pui ), 0 );
    return ( n == (ssize_t)sizeof( *pui ) ) ? 0 : errno;
}

//  Secure-CRT printf family. MSVC's `_s` variants take a buffer-size arg
//  followed by a count arg; we map them onto POSIX swprintf/vswprintf/
//  snprintf/vsnprintf which take buffer size only. The truncation /
//  NUL-handling semantics are not byte-identical to MSVC's hard checks,
//  but matches what the engine relies on at call sites.
//
// libc's swprintf takes 32-bit wchar_t; under -fshort-wchar it writes
// 4 bytes per output char into a 2-byte-per-char buffer → buffer
// overflow + garbage. Route every wide formatter to StringCb*PrintfW
// (16-bit-aware, in winapi_strsafe.cxx). Cb* takes byte count, so
// scale the wchar count by sizeof(wchar_t).
#ifdef __cplusplus
extern "C" {
#endif
HRESULT StringCbVPrintfW( wchar_t* dst, size_t cbDst, const wchar_t* fmt, va_list args );
HRESULT StringCbPrintfW(  wchar_t* dst, size_t cbDst, const wchar_t* fmt, ... );
#ifdef __cplusplus
}
#endif
#define swprintf_s( dst, cnt, fmt, ... )  StringCbPrintfW( (dst), (cnt) * sizeof( wchar_t ), (fmt), ##__VA_ARGS__ )
#define vswprintf_s( dst, cnt, fmt, ap )  StringCbVPrintfW( (dst), (cnt) * sizeof( wchar_t ), (fmt), (ap) )
#define _snwprintf_s( dst, sz, cnt, fmt, ... ) StringCbPrintfW( (dst), (sz), (fmt), ##__VA_ARGS__ )
#define _vsnwprintf_s( dst, sz, cnt, fmt, ap ) StringCbVPrintfW( (dst), (sz), (fmt), (ap) )
// _snprintf_s already defined in cc.hxx (with min(cb,cnt) — slightly
// safer than this earlier shim form). Avoid the duplicate definition
// (Wmacro-redefined) by skipping it here.
#define _vsnprintf_s( dst, sz, cnt, fmt, ap )  vsnprintf( (dst), (sz), (fmt), (ap) )

//  MSVC stack-allocator macros. _malloca normally falls back to heap for
//  large requests; we always stack-allocate via alloca() because the
//  engine call sites are bounded (single struct sizes ≤ a few hundred
//  bytes). _freea matches because alloca'd memory is freed at frame exit.
//
#define _malloca( size )    alloca( (size) )
#define _freea( p )         ( (void)( p ) )

//  CRT path-splitter constants (live in <stdlib.h> on Windows). eseutil
//  uses these as static buffer sizes for split-path scratch. Values match
//  the historical MSVC <stdlib.h> definitions.
//
#ifndef _MAX_PATH
#define _MAX_PATH       260
#define _MAX_DRIVE      3
#define _MAX_DIR        256
#define _MAX_FNAME      256
#define _MAX_EXT        256
#endif
#ifndef MAX_PATH
#define MAX_PATH        260
#endif

//  RTL_NUMBER_OF — NT/Windows analogue of _countof. Used by eseutil to size
//  static arg/option tables.
//
#ifndef RTL_NUMBER_OF
#define RTL_NUMBER_OF( a )      ( sizeof( a ) / sizeof( ( a )[ 0 ] ) )
#endif

//  Heap APIs. eseutil calls GetProcessHeap/HeapAlloc/HeapFree for transient
//  buffers (NTFS extent enumeration scratch) and HeapEnableTermination-
//  OnCorruption as a hardening hint at startup. Map to malloc/free; the
//  hardening hint is a no-op on Linux (glibc has its own checks).
//
#ifdef __cplusplus
extern "C" {
#endif

#ifndef HEAP_ZERO_MEMORY
#define HEAP_ZERO_MEMORY    0x00000008
#endif
#ifndef HeapEnableTerminationOnCorruption
#define HeapEnableTerminationOnCorruption   1
#endif

typedef void* HANDLE_HEAP;

static inline HANDLE GetProcessHeap( void )
{
    return (HANDLE)(uintptr_t)1;
}

static inline void* HeapAlloc( HANDLE hHeap, DWORD dwFlags, size_t cb )
{
    (void)hHeap;
    void* p = malloc( cb );
    if ( p && ( dwFlags & HEAP_ZERO_MEMORY ) ) memset( p, 0, cb );
    return p;
}

static inline BOOL HeapFree( HANDLE hHeap, DWORD dwFlags, void* p )
{
    (void)hHeap;
    (void)dwFlags;
    free( p );
    return TRUE;
}

static inline BOOL HeapSetInformation( HANDLE hHeap, int Class, void* Info, size_t cbInfo )
{
    (void)hHeap; (void)Class; (void)Info; (void)cbInfo;
    return TRUE;
}

//  System info. Only the fields eseutil reads (page size, processor count)
//  are meaningful on Linux — fill the rest with sentinels.
//
typedef struct _SYSTEM_INFO {
    union {
        DWORD dwOemId;
        struct {
            WORD wProcessorArchitecture;
            WORD wReserved;
        };
    };
    DWORD dwPageSize;
    void* lpMinimumApplicationAddress;
    void* lpMaximumApplicationAddress;
    DWORD dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD  wProcessorLevel;
    WORD  wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

void GetSystemInfo( LPSYSTEM_INFO lpSystemInfo );

//  CopyFileExW progress callback. eseutil's copyfile mode wires up a
//  progress UI; the Linux impl ignores the callback parameters and copies
//  the file with sendfile()/posix_fadvise(). Tokens are kept as #defines so
//  callers compile.
//
#ifndef PROGRESS_CONTINUE
#define PROGRESS_CONTINUE       0
#define PROGRESS_CANCEL         1
#define PROGRESS_STOP           2
#define PROGRESS_QUIET          3
#endif

#ifndef CALLBACK_CHUNK_FINISHED
#define CALLBACK_CHUNK_FINISHED 0x00000000
#define CALLBACK_STREAM_SWITCH  0x00000001
#endif

#ifndef COPY_FILE_FAIL_IF_EXISTS
#define COPY_FILE_FAIL_IF_EXISTS    0x00000001
#define COPY_FILE_RESTARTABLE       0x00000002
#endif

typedef DWORD ( __stdcall *LPPROGRESS_ROUTINE )(
    LARGE_INTEGER TotalFileSize,
    LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER StreamSize,
    LARGE_INTEGER StreamBytesTransferred,
    DWORD         dwStreamNumber,
    DWORD         dwCallbackReason,
    HANDLE        hSourceFile,
    HANDLE        hDestinationFile,
    LPVOID        lpData );

BOOL CopyFileExW( const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName,
                  LPPROGRESS_ROUTINE lpProgressRoutine, LPVOID lpData,
                  LPBOOL pbCancel, DWORD dwCopyFlags );

//  NTFS extent enumeration. ESEUTIL's `/m` file-info mode dumps the on-disk
//  extent layout via DeviceIoControl( FSCTL_GET_RETRIEVAL_POINTERS ). On
//  Linux there is no equivalent IOCTL surface that ESE cares about — the
//  callers are gated `#ifdef _WIN32` in eseutil.cxx, but the type names
//  still need to parse.
//
#ifndef FSCTL_GET_RETRIEVAL_POINTERS
#define FSCTL_GET_RETRIEVAL_POINTERS    0
#endif

typedef struct {
    LARGE_INTEGER StartingVcn;
} STARTING_VCN_INPUT_BUFFER, *PSTARTING_VCN_INPUT_BUFFER;

typedef struct {
    DWORD         ExtentCount;
    LARGE_INTEGER StartingVcn;
    struct {
        LARGE_INTEGER NextVcn;
        LARGE_INTEGER Lcn;
    } Extents[1];
} RETRIEVAL_POINTERS_BUFFER, *PRETRIEVAL_POINTERS_BUFFER;

//  Path normalization. _wfullpath resolves relative paths against the cwd.
//  POSIX equivalent realpath() requires the path to exist; eseutil calls
//  it on candidate database/log names before they exist, so the impl in
//  os/posix/strsafe_posix.cxx fabricates a result by joining cwd with the
//  given path when realpath() fails with ENOENT.
//
wchar_t* _wfullpath( wchar_t* absPath, const wchar_t* relPath, size_t maxLen );

//  CRT secure-string wide variants. eseutil uses these to copy/cat WCHAR
//  buffers with explicit destination size. Implementations live in
//  os/posix/strsafe_posix.cxx (alongside the StringCb*/StringCch* family).
//
errno_t wcscpy_s( wchar_t* dst, size_t cchDst, const wchar_t* src );
errno_t wcscat_s( wchar_t* dst, size_t cchDst, const wchar_t* src );
errno_t strcpy_s( char* dst, size_t cchDst, const char* src );
errno_t strcat_s( char* dst, size_t cchDst, const char* src );
errno_t _wcsupr_s( wchar_t* str, size_t cchStr );


//  Wide secure-CRT scanf. Engine wchar_t is 16-bit (-fshort-wchar) so glibc's
//  swscanf (32-bit wchar_t) is unusable — we provide a custom impl that
//  parses signed/unsigned integers with %d/%u/%ld/%lu and a single %c.
//  Sufficient for eseutil's option parsing.
//
int _snwscanf_s( const wchar_t* buffer, size_t cchCount, const wchar_t* fmt, ... );

#ifdef __cplusplus
}  // extern "C"

//  MSVC-style array-overload variants of the secure-CRT *_s family.
//  The Microsoft secure-CRT provides 2-arg overloads that deduce the
//  destination size from an array reference; engine code (and the test
//  scaffolding in jettest.cxx) uses them interchangeably with the 3-arg
//  forms.  Must live outside extern "C" — templates need C++ linkage.
template < size_t N >
inline errno_t wcscpy_s( wchar_t (&dst)[ N ], const wchar_t* src ) { return wcscpy_s( dst, N, src ); }
template < size_t N >
inline errno_t wcscat_s( wchar_t (&dst)[ N ], const wchar_t* src ) { return wcscat_s( dst, N, src ); }
template < size_t N >
inline errno_t strcpy_s( char (&dst)[ N ], const char* src )       { return strcpy_s( dst, N, src ); }
template < size_t N >
inline errno_t strcat_s( char (&dst)[ N ], const char* src )       { return strcat_s( dst, N, src ); }
#endif

//  Win32 NLS surface — provided by libnls (LGPL shared library at
//  /p/ese/nls/).  Engine code transitively pulls this in via
//  windows.h, matching Win32 convention.  All prereqs the header
//  needs live in libnls's vendored winnls.h itself.
#include <winnls.h>
