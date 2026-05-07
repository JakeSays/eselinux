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
};

KObject* AllocKObject( HandleKind kind );
void     FreeKObject( KObject* k );

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
