// Linux shim for <winnt.h>. Provides the Win32 base type surface the ESE
// OS internal headers (_osdisk.hxx, _osfile.hxx, _osfs.hxx, ...) reference
// at parse time. Real implementations / behaviours land in os/posix/.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cc.hxx"  // for BYTE/WORD/DWORD/ULONG/ULONGLONG/LONG_PTR/... (also pulls in platform.h)
#include "guiddef.h"  // for GUID (used in FILE_ID_DESCRIPTOR's union)

#ifdef __cplusplus
extern "C" {
#endif

//  Boolean variants
//
typedef BYTE                BOOLEAN;
typedef BOOLEAN             *PBOOLEAN;

//  Win32 message-loop word/pointer-sized aliases (windef.h on Windows).
//  Engine references LPARAM / WPARAM only as opaque arg types.
//
typedef LONG_PTR            LPARAM;
typedef UINT_PTR            WPARAM;
typedef LONG_PTR            LRESULT;

#ifndef TRUE
#define TRUE                1
#endif
#ifndef FALSE
#define FALSE               0
#endif

//  Status / result types. NTSTATUS / HRESULT use LONG (32-bit) — matching
//  Windows. Don't use `long` here: on LP64 Linux that is 64-bit and the
//  engine source re-typedefs NTSTATUS as `LONG`, which would clash.
//
typedef LONG                NTSTATUS;
#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef LONG                HRESULT;
#endif

//  Void / handle types
//
#ifndef VOID
#define VOID void
#endif
typedef void*               PVOID;
typedef void*               PVOID64;        // Win32 32-bit ABI distinction; on LP64 Linux it's just a pointer.
typedef const void*         PCVOID;
typedef void*               LPVOID;
typedef const void*         LPCVOID;

typedef void*               HANDLE;
typedef HANDLE*             PHANDLE;
typedef HANDLE*             LPHANDLE;
typedef HANDLE              HMODULE;
typedef HANDLE              HINSTANCE;
typedef HANDLE              HKEY;
typedef HANDLE              HLOCAL;
typedef HANDLE              HGLOBAL;

//  WCHAR / string-pointer aliases (CHAR/LPSTR live in cc.hxx). Single source of
//  truth shared with the published os.hxx-chain headers.
#include "commontypes.hxx"

//  "Long pointer" aliases for the Win32 base integer types.
//
typedef BYTE*               LPBYTE;
typedef WORD*               LPWORD;
typedef DWORD*              LPDWORD;
typedef LONG*               LPLONG;
typedef BOOL*               LPBOOL;
typedef BOOL*               PBOOL;

//  Locale / language identifiers.  LCID is a packed (LANGID, SORTID)
//  pair; LANGID a packed (PRIMARYLANGID, SUBLANGID) pair.  These are
//  fundamental Win32 types — winnls.h references them but doesn't
//  define them itself.
typedef WORD                LANGID;
typedef DWORD               LCID;

//  64-bit integer unions
//
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    struct {
        DWORD LowPart;
        LONG  HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

//  Time structures
//
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

//  Scatter/gather I/O element (Win32: union of buffer pointer / 8-byte
//  alignment for 64-bit). Used by ReadFileScatter / WriteFileGather.
//
typedef union _FILE_SEGMENT_ELEMENT {
    PVOID64 Buffer;
    ULONGLONG Alignment;
} FILE_SEGMENT_ELEMENT, *PFILE_SEGMENT_ELEMENT;

//  Asynchronous I/O
//
typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

//  Critical section (opaque to engine; real fields filled in posix layer)
//
typedef struct _RTL_CRITICAL_SECTION {
    PVOID    DebugInfo;
    LONG     LockCount;
    LONG     RecursionCount;
    HANDLE   OwningThread;
    HANDLE   LockSemaphore;
    ULONG_PTR SpinCount;
} RTL_CRITICAL_SECTION, CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

//  Security
//
typedef struct _SECURITY_ATTRIBUTES {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef PVOID PSECURITY_DESCRIPTOR;

//  Reference-suppression macros (winnt.h on Windows). UNREFERENCED_*
//  silence "unused parameter" warnings without changing semantics.
//
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P)           ((void)(P))
#define DBG_UNREFERENCED_PARAMETER(P)       ((void)(P))
#define DBG_UNREFERENCED_LOCAL_VARIABLE(V)  ((void)(V))
#endif

//  GetExitCodeThread sentinel — the thread is still running when the
//  exit code equals STILL_ACTIVE (== STATUS_PENDING).
//
#ifndef STILL_ACTIVE
#define STILL_ACTIVE  ((DWORD)0x00000103L)
#endif

//  Calling convention / DLL-import macros (no-ops on Linux)
//
#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef NTAPI
#define NTAPI
#endif
#ifndef WINAPIV
#define WINAPIV
#endif
#ifndef WINBASEAPI
#define WINBASEAPI
#endif
#ifndef WINADVAPI
#define WINADVAPI
#endif
#ifndef WINUSERAPI
#define WINUSERAPI
#endif
#ifndef NTSYSAPI
#define NTSYSAPI
#endif
#ifndef NTSYSCALLAPI
#define NTSYSCALLAPI
#endif
#ifndef __kernel_entry
#define __kernel_entry
#endif

//  Win32 security / privilege names.  On Linux there's no token-based
//  privilege model — these are declared so engine code compiles, but
//  the LookupPrivilegeValueW / AdjustTokenPrivileges shims all fail
//  with ERROR_NOT_SUPPORTED.  Engine call sites treat the failure as
//  "feature unavailable" and fall through to the non-privileged path
//  (e.g., SetFileValidData -> ftruncate-and-zero-fill instead of
//  zero-cost extend).
#ifndef SE_MANAGE_VOLUME_NAME
#define SE_MANAGE_VOLUME_NAME                   "SeManageVolumePrivilege"
#define SE_BACKUP_NAME                          "SeBackupPrivilege"
#define SE_RESTORE_NAME                         "SeRestorePrivilege"
#define SE_DEBUG_NAME                           "SeDebugPrivilege"
#define SE_LOCK_MEMORY_NAME                     "SeLockMemoryPrivilege"
#define SE_INCREASE_QUOTA_NAME                  "SeIncreaseQuotaPrivilege"
#define SE_TCB_NAME                             "SeTcbPrivilege"
#define SE_SECURITY_NAME                        "SeSecurityPrivilege"
#define SE_TAKE_OWNERSHIP_NAME                  "SeTakeOwnershipPrivilege"
#define SE_LOAD_DRIVER_NAME                     "SeLoadDriverPrivilege"
#define SE_SYSTEM_PROFILE_NAME                  "SeSystemProfilePrivilege"
#define SE_SYSTEMTIME_NAME                      "SeSystemtimePrivilege"
#define SE_PROF_SINGLE_PROCESS_NAME             "SeProfileSingleProcessPrivilege"
#define SE_INC_BASE_PRIORITY_NAME               "SeIncreaseBasePriorityPrivilege"
#define SE_CREATE_PAGEFILE_NAME                 "SeCreatePagefilePrivilege"
#define SE_SHUTDOWN_NAME                        "SeShutdownPrivilege"
#define SE_AUDIT_NAME                           "SeAuditPrivilege"
#define SE_CHANGE_NOTIFY_NAME                   "SeChangeNotifyPrivilege"
#define SE_REMOTE_SHUTDOWN_NAME                 "SeRemoteShutdownPrivilege"
#define SE_UNDOCK_NAME                          "SeUndockPrivilege"
#endif

#ifndef SE_PRIVILEGE_ENABLED
#define SE_PRIVILEGE_ENABLED                    0x00000002u
#define SE_PRIVILEGE_ENABLED_BY_DEFAULT         0x00000001u
#define SE_PRIVILEGE_USED_FOR_ACCESS            0x80000000u
#endif

#ifndef TOKEN_QUERY
#define TOKEN_ASSIGN_PRIMARY                    0x0001u
#define TOKEN_DUPLICATE                         0x0002u
#define TOKEN_IMPERSONATE                       0x0004u
#define TOKEN_QUERY                             0x0008u
#define TOKEN_QUERY_SOURCE                      0x0010u
#define TOKEN_ADJUST_PRIVILEGES                 0x0020u
#define TOKEN_ADJUST_GROUPS                     0x0040u
#define TOKEN_ADJUST_DEFAULT                    0x0080u
#define TOKEN_ADJUST_SESSIONID                  0x0100u
#endif

//  UINT8/16/32 + pointer aliases — Win32 has them in basetsd.h.  The
//  64-bit (UINT64/INT64/PUINT64/PINT64) twins are defined in cc.hxx
//  matching `unsigned long long` / `long long` so they avoid the
//  uint64_t-vs-unsigned-long-long type clash on x86_64 LP64.
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int8_t   INT8;
typedef int16_t  INT16;
typedef int32_t  INT32;
typedef UINT8*   PUINT8;
typedef UINT16*  PUINT16;
typedef UINT32*  PUINT32;
typedef INT8*    PINT8;
typedef INT16*   PINT16;
typedef INT32*   PINT32;

typedef struct _LUID {
    DWORD LowPart;
    LONG  HighPart;
} LUID, *PLUID;

typedef struct _LUID_AND_ATTRIBUTES {
    LUID  Luid;
    DWORD Attributes;
} LUID_AND_ATTRIBUTES, *PLUID_AND_ATTRIBUTES;

typedef struct _TOKEN_PRIVILEGES {
    DWORD               PrivilegeCount;
    LUID_AND_ATTRIBUTES Privileges[ 1 ];
} TOKEN_PRIVILEGES, *PTOKEN_PRIVILEGES;
#ifndef WINOLEAPI
#define WINOLEAPI
#endif
#ifndef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT
#endif
// MSVC __declspec(selectany) marks a global as "any one of these duplicate
// definitions wins at link time". Clang accepts the same spelling on ELF
// targets, but the engine writes the SAL-style alias DECLSPEC_SELECTANY.
#ifndef DECLSPEC_SELECTANY
#define DECLSPEC_SELECTANY __attribute__((selectany))
#endif
#ifndef DECLSPEC_NOINLINE
#define DECLSPEC_NOINLINE __attribute__((noinline))
#endif
#ifndef DECLSPEC_NORETURN
#define DECLSPEC_NORETURN __attribute__((noreturn))
#endif
#ifndef DECLSPEC_DEPRECATED
#define DECLSPEC_DEPRECATED __attribute__((deprecated))
#endif
#ifndef DECLSPEC_ALIGN
#define DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
#endif

//  Common return-type macros
//
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef FAR
#define FAR
#endif
#ifndef NEAR
#define NEAR
#endif

//  Invalid handle sentinel (Win32 = (HANDLE)-1)
//
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif

//  Maximum path length (windef.h on Windows). Engine call sites size
//  WCHAR buffers with this constant. Linux's PATH_MAX (4096) is larger,
//  but matching MSVC's value keeps file-format / on-disk struct sizes
//  identical across platforms.
//
#ifndef MAX_PATH
#define MAX_PATH        260
#endif

//  Standard access-right bits (winnt.h on Windows). Engine call sites pass
//  these to OpenThread / OpenProcess; the posix layer interprets them when
//  emulating handles.
//
#ifndef SYNCHRONIZE
#define DELETE                   0x00010000L
#define READ_CONTROL             0x00020000L
#define WRITE_DAC                0x00040000L
#define WRITE_OWNER              0x00080000L
#define SYNCHRONIZE              0x00100000L
#define STANDARD_RIGHTS_REQUIRED 0x000F0000L
#define STANDARD_RIGHTS_READ     READ_CONTROL
#define STANDARD_RIGHTS_WRITE    READ_CONTROL
#define STANDARD_RIGHTS_EXECUTE  READ_CONTROL
#define STANDARD_RIGHTS_ALL      0x001F0000L
#define SPECIFIC_RIGHTS_ALL      0x0000FFFFL
#endif

//  Generic-access mapping (winnt.h on Windows). CreateFile / file-system
//  filter call sites pass these as dwDesiredAccess.
//
#ifndef GENERIC_READ
#define GENERIC_READ        0x80000000L
#define GENERIC_WRITE       0x40000000L
#define GENERIC_EXECUTE     0x20000000L
#define GENERIC_ALL         0x10000000L
#endif

//  File-share, file-attribute, and file-flag bits (winnt.h on Windows).
//  CreateFileW dwShareMode / dwFlagsAndAttributes consumers.
//
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ                     0x00000001
#define FILE_SHARE_WRITE                    0x00000002
#define FILE_SHARE_DELETE                   0x00000004
#endif

#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY             0x00000001
#define FILE_ATTRIBUTE_HIDDEN               0x00000002
#define FILE_ATTRIBUTE_SYSTEM               0x00000004
#define FILE_ATTRIBUTE_DIRECTORY            0x00000010
#define FILE_ATTRIBUTE_ARCHIVE              0x00000020
#define FILE_ATTRIBUTE_DEVICE               0x00000040
#define FILE_ATTRIBUTE_NORMAL               0x00000080
#define FILE_ATTRIBUTE_TEMPORARY            0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE          0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT        0x00000400
#define FILE_ATTRIBUTE_COMPRESSED           0x00000800
#define FILE_ATTRIBUTE_OFFLINE              0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED  0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED            0x00004000
#define INVALID_FILE_ATTRIBUTES             ((DWORD)-1)
#endif

#ifndef FILE_FLAG_WRITE_THROUGH
#define FILE_FLAG_WRITE_THROUGH         0x80000000
#define FILE_FLAG_OVERLAPPED            0x40000000
#define FILE_FLAG_NO_BUFFERING          0x20000000
#define FILE_FLAG_RANDOM_ACCESS         0x10000000
#define FILE_FLAG_SEQUENTIAL_SCAN       0x08000000
#define FILE_FLAG_DELETE_ON_CLOSE       0x04000000
#define FILE_FLAG_BACKUP_SEMANTICS      0x02000000
#define FILE_FLAG_POSIX_SEMANTICS       0x01000000
#endif

//  Specific file-object access rights (winnt.h on Windows). Subset used
//  by the engine's file-system filter to interpret CreateFile dwAccess.
//
#ifndef FILE_READ_DATA
#define FILE_READ_DATA              0x0001
#define FILE_WRITE_DATA             0x0002
#define FILE_APPEND_DATA            0x0004
#define FILE_READ_EA                0x0008
#define FILE_WRITE_EA               0x0010
#define FILE_EXECUTE                0x0020
#define FILE_READ_ATTRIBUTES        0x0080
#define FILE_WRITE_ATTRIBUTES       0x0100
#define FILE_ALL_ACCESS             ( STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 0x1FF )
#endif

//  CreateFile dwCreationDisposition values (fileapi.h on Windows).
//
#ifndef CREATE_NEW
#define CREATE_NEW          1
#define CREATE_ALWAYS       2
#define OPEN_EXISTING       3
#define OPEN_ALWAYS         4
#define TRUNCATE_EXISTING   5
#endif

//  SetFilePointer dwMoveMethod values.
//
#ifndef FILE_BEGIN
#define FILE_BEGIN          0
#define FILE_CURRENT        1
#define FILE_END            2
#endif

//  RtlCompressBuffer / RtlDecompressBuffer format & engine flags
//  (winnt.h on Windows).
//
#ifndef COMPRESSION_FORMAT_NONE
#define COMPRESSION_FORMAT_NONE         0x0000
#define COMPRESSION_FORMAT_DEFAULT      0x0001
#define COMPRESSION_FORMAT_LZNT1        0x0002
#define COMPRESSION_FORMAT_XPRESS       0x0003
#define COMPRESSION_FORMAT_XPRESS_HUFF  0x0004
#define COMPRESSION_ENGINE_STANDARD     0x0000
#define COMPRESSION_ENGINE_MAXIMUM      0x0100
#define COMPRESSION_ENGINE_HIBER        0x0200
#endif

//  GetFileInformationByHandleEx classes & shapes (winbase.h / fileapi.h
//  on Windows). Engine call sites use FileIdInfo to read a 128-bit file
//  identifier on ReFS volumes; the posix layer maps this to (st_dev,
//  st_ino) packed into the 128-bit field.
//
typedef struct _FILE_ID_128 {
    BYTE Identifier[16];
} FILE_ID_128, *PFILE_ID_128;

typedef struct _FILE_ID_INFO {
    ULONGLONG    VolumeSerialNumber;
    FILE_ID_128  FileId;
} FILE_ID_INFO, *PFILE_ID_INFO;

//  OpenFileById descriptor (winbase.h on Windows). Engine uses Type ==
//  FileIdType (64-bit) for legacy NTFS handles and ExtendedFileIdType
//  (128-bit) for ReFS.
//
typedef enum _FILE_ID_TYPE {
    FileIdType         = 0,
    ObjectIdType       = 1,
    ExtendedFileIdType = 2,
    MaximumFileIdType
} FILE_ID_TYPE, *PFILE_ID_TYPE;

typedef struct _FILE_ID_DESCRIPTOR {
    DWORD        dwSize;
    FILE_ID_TYPE Type;
    union {
        LARGE_INTEGER FileId;
        GUID          ObjectId;
        FILE_ID_128   ExtendedFileId;
    };
} FILE_ID_DESCRIPTOR, *LPFILE_ID_DESCRIPTOR;

#ifndef _FILE_INFO_BY_HANDLE_CLASS_DEFINED
#define _FILE_INFO_BY_HANDLE_CLASS_DEFINED
typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileBasicInfo                   = 0,
    FileStandardInfo                = 1,
    FileNameInfo                    = 2,
    FileRenameInfo                  = 3,
    FileDispositionInfo             = 4,
    FileAllocationInfo              = 5,
    FileEndOfFileInfo               = 6,
    FileStreamInfo                  = 7,
    FileCompressionInfo             = 8,
    FileAttributeTagInfo            = 9,
    FileIdBothDirectoryInfo         = 10,
    FileIdBothDirectoryRestartInfo  = 11,
    FileIoPriorityHintInfo          = 12,
    FileRemoteProtocolInfo          = 13,
    FileFullDirectoryInfo           = 14,
    FileFullDirectoryRestartInfo    = 15,
    FileStorageInfo                 = 16,
    FileAlignmentInfo               = 17,
    FileIdInfo                      = 18,
    FileIdExtdDirectoryInfo         = 19,
    FileIdExtdDirectoryRestartInfo  = 20,
    MaximumFileInfoByHandleClass
} FILE_INFO_BY_HANDLE_CLASS, *PFILE_INFO_BY_HANDLE_CLASS;

//  FILE_RENAME_INFO is the payload for SetFileInformationByHandle with
//  FileRenameInfo.  Win32 declares the FileName trailing array as
//  WCHAR FileName[1]; engine code reserves the additional bytes via the
//  sizeof + path-length computation, then writes through pRenameInfo->FileName.
typedef struct _FILE_RENAME_INFO {
    union {
        BOOLEAN ReplaceIfExists;
        DWORD   Flags;
    };
    HANDLE      RootDirectory;
    DWORD       FileNameLength;
    WCHAR       FileName[ 1 ];
} FILE_RENAME_INFO, *PFILE_RENAME_INFO;

typedef struct _FILE_END_OF_FILE_INFO {
    LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFO, *PFILE_END_OF_FILE_INFO;

typedef struct _FILE_BASIC_INFO {
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    DWORD         FileAttributes;
} FILE_BASIC_INFO, *PFILE_BASIC_INFO;

typedef struct _FILE_STANDARD_INFO {
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER EndOfFile;
    DWORD         NumberOfLinks;
    BOOLEAN       DeletePending;
    BOOLEAN       Directory;
} FILE_STANDARD_INFO, *PFILE_STANDARD_INFO;

typedef struct _FILE_ALLOCATION_INFO {
    LARGE_INTEGER AllocationSize;
} FILE_ALLOCATION_INFO, *PFILE_ALLOCATION_INFO;

typedef struct _FILE_DISPOSITION_INFO {
    BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFO, *PFILE_DISPOSITION_INFO;

#endif

//  GetDriveType return values (fileapi.h on Windows). The Linux port maps
//  Linux drive types (block/USB/iSCSI/etc.) onto these for diagnostic
//  parity with Windows behaviour.
//
#ifndef DRIVE_UNKNOWN
#define DRIVE_UNKNOWN       0
#define DRIVE_NO_ROOT_DIR   1
#define DRIVE_REMOVABLE     2
#define DRIVE_FIXED         3
#define DRIVE_REMOTE        4
#define DRIVE_CDROM         5
#define DRIVE_RAMDISK       6
#endif

//  Public Win32 Interlocked* aliases. The engine's sync.hxx already
//  provides the underscore-prefixed intrinsic-style names (which clang
//  exposes as builtins under -fms-extensions). A handful of source files
//  use the public Win32 names without the underscore — wire those up.
//
#define InterlockedCompareExchange      _InterlockedCompareExchange
#define InterlockedCompareExchange16    _InterlockedCompareExchange16
#define InterlockedCompareExchange64    _InterlockedCompareExchange64
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#define InterlockedExchange             _InterlockedExchange
#define InterlockedExchange64(p, v)     _InterlockedExchange64( reinterpret_cast<volatile long*>( p ), (v) )
#define InterlockedExchangePointer      _InterlockedExchangePointer
#define InterlockedExchangeAdd          _InterlockedExchangeAdd
#define InterlockedExchangeAdd16        _InterlockedExchangeAdd16
#define InterlockedExchangeAdd64(p, v)  _InterlockedExchangeAdd64( reinterpret_cast<volatile long*>( p ), (v) )
#define InterlockedIncrement            _InterlockedIncrement
// On x86_64 Linux clang's _Interlocked*64 builtins take `volatile long*`,
// but engine + test code passes `volatile LONGLONG*` (long long). The
// types are the same width but distinct under C++ overload resolution.
// Wrap each macro with a reinterpret_cast so call sites don't need to
// know the target type.
#define InterlockedIncrement64(p)       _InterlockedIncrement64( reinterpret_cast<volatile long*>( p ) )
#define InterlockedDecrement            _InterlockedDecrement
#define InterlockedDecrement64(p)       _InterlockedDecrement64( reinterpret_cast<volatile long*>( p ) )
#define InterlockedAnd                  _InterlockedAnd
#define InterlockedOr                   _InterlockedOr
#define InterlockedXor                  _InterlockedXor

#ifdef __cplusplus
}
#endif
