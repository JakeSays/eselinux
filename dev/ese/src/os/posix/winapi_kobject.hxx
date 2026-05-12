// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal: kernel-object backing for the Win32 HANDLE surface. Used by
// winapi_handle.cxx, winapi_sync.cxx, winapi_thread.cxx. Not exported
// outside the posix layer.
#pragma once

#include "osstd.hxx"

#include <atomic>
#include <pthread.h>

namespace osposix
{

enum class HandleKind : int
{
    Event,
    Mutex,
    Semaphore,
    Thread,
    File,
    FindFile,
    FindVolume,
    FileMapping,
    BlockDevice,   // synthesized for \\?\Volume{...} and \\.\PHYSICALDRIVE{N}
};

struct KObject
{
    HandleKind kind;
    std::atomic<int> refCount;

    pthread_mutex_t lock;
    pthread_cond_t  cond;

    // ---- Event ----
    bool eventSignaled;
    bool eventManualReset;

    // ---- Mutex ----
    bool      mutexAbandoned;
    pthread_t mutexOwner;        // value undefined when held==false
    bool      mutexHeld;
    int       mutexRecursion;

    // ---- Semaphore ----
    long semCurrent;
    long semMax;

    // ---- Thread ----
    pthread_t thread;
    DWORD     threadExitCode;
    bool      threadFinished;
    LPTHREAD_START_ROUTINE threadStart;
    LPVOID                 threadParam;
    bool      threadSuspended;
    int       threadPriority;
    bool      threadPriorityBoostDisabled;

    // ---- File ----
    int       fileFd;
    DWORD     fileFlagsAndAttrs;     // CreateFile dwFlagsAndAttributes
    DWORD     fileDesiredAccess;     // GENERIC_READ/WRITE — used by Read/WriteFile fast checks
    char*     fileDeleteOnClosePath; // owned, free()'d in FreeKObject; null if not requested
    char*     fileOpenedPath;        // owned, narrow UTF-8, used by GetFinalPathNameByHandleW

    // ---- FindFile / FindVolume ----
    void*     findDir;               // DIR* — opendir handle
    char*     findBaseDir;           // owned narrow UTF-8 directory the wildcard resolved to
    char*     findPattern;           // owned narrow UTF-8 pattern (basename of path); null = "*"
    int       findVolumeIndex;       // FindNextVolumeW iteration cursor

    // ---- FileMapping (CreateFileMappingW result) ----
    int       mappingFd;             // dup'd file fd; closed when the mapping handle is closed
    DWORD     mappingProtect;        // PAGE_READONLY / PAGE_READWRITE (translated to mmap PROT)
    QWORD     mappingMaxSize;        // max view size; 0 means "to end of file"

    // ---- BlockDevice (CreateFileW on \\?\Volume{X-Y} or \\.\PHYSICALDRIVE{N}) ----
    //
    // Synthesized handle that carries the (major,minor) of the underlying
    // filesystem or whole-disk device, plus the kernel device name (e.g.
    // "sda1" or "sda" — no /dev/ prefix).  DeviceIoControl uses these to
    // answer IOCTL_STORAGE_QUERY_PROPERTY / IOCTL_DISK_GET_CACHE_INFORMATION
    // / IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS by reading /sys/block/<name>/...
    // No real fd is held — the kernel-name suffices for sysfs lookups.
    unsigned int blockMajor;
    unsigned int blockMinor;
    unsigned int blockDiskMajor;     // whole-disk parent — equals blockMajor/Minor if already a whole disk
    unsigned int blockDiskMinor;
    char*        blockDiskName;      // owned narrow UTF-8, no /dev/ prefix (e.g. "sda")
};

KObject* AllocKObject( HandleKind kind );
void     FreeKObject( KObject* k );

//  Block-device synthesis: see winapi_blockdev.cxx.
bool     ResolveBlockDeviceForPath( const char* path,
                                    unsigned int* pFsMajor, unsigned int* pFsMinor,
                                    unsigned int* pDiskMajor, unsigned int* pDiskMinor,
                                    char* diskName, size_t cchName );
KObject* AllocBlockDeviceKObject( unsigned int fsMajor, unsigned int fsMinor,
                                  unsigned int diskMajor, unsigned int diskMinor,
                                  char* diskName );

inline KObject* HandleToK( HANDLE h )
{
    return static_cast<KObject*>( h );
}

inline HANDLE KToHandle( KObject* k )
{
    return static_cast<HANDLE>( k );
}

// Wait machinery. Returns WAIT_OBJECT_0 / WAIT_TIMEOUT / WAIT_ABANDONED /
// WAIT_FAILED. Acquires the object on success (auto-reset event clears,
// mutex takes ownership, semaphore decrements). Caller must NOT hold the
// object's lock.
DWORD WaitOne( KObject* k, DWORD dwMilliseconds );

}  // namespace osposix
