// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Kernel-object lifecycle and wait machinery behind the Win32 HANDLE
// surface. Constructors/destructors own each kind's resources (replacing the
// old AllocKObject/FreeKObject switch); the per-kind methods carry the
// behavior (replacing the old WaitOne switch and the thread ops that used to
// poke fields from winapi_thread.cxx). The Win32 entry points that wrap these
// live in winapi_handle.cxx (close / duplicate), winapi_sync.cxx (event /
// mutex / semaphore), and winapi_thread.cxx (thread).

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

namespace osposix
{

// ---- deadline helpers ----------------------------------------------------

void GetMonotonicDeadline(DWORD dwMilliseconds, struct timespec* deadline)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += dwMilliseconds / 1000;
    deadline->tv_nsec += static_cast<long>(dwMilliseconds % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L)
    {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
}

bool DeadlinePassed(const struct timespec& deadline)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec != deadline.tv_sec)
    {
        return now.tv_sec > deadline.tv_sec;
    }
    return now.tv_nsec > deadline.tv_nsec;
}

// ---- KObject -------------------------------------------------------------

KObject::KObject(HandleKind kind)
    : _kind(kind),
      _refCount(1)
{
}

KObject::~KObject()
{
}

void KObject::Reference()
{
    _refCount.fetch_add(1, std::memory_order_relaxed);
}

bool KObject::Dereference()
{
    return _refCount.fetch_sub(1, std::memory_order_acq_rel) == 1;
}

DWORD KObject::Wait(DWORD)
{
    // File / find / mapping / blockdev handles aren't waitable; fail
    // explicitly rather than blocking on a condvar that never signals.
    return WAIT_FAILED;
}

// ---- WaitableObject ------------------------------------------------------

WaitableObject::WaitableObject(HandleKind kind)
    : KObject(kind)
{
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&_lock, &mattr);
    pthread_mutexattr_destroy(&mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&_cond, &cattr);
    pthread_condattr_destroy(&cattr);
}

WaitableObject::~WaitableObject()
{
    pthread_cond_destroy(&_cond);
    pthread_mutex_destroy(&_lock);
}

template<class Pred>
int WaitableObject::WaitWhile(DWORD dwMilliseconds, Pred pred)
{
    if (dwMilliseconds == INFINITE)
    {
        while (pred())
        {
            pthread_cond_wait(&_cond, &_lock);
        }
        return 0;
    }
    struct timespec deadline;
    GetMonotonicDeadline(dwMilliseconds, &deadline);
    while (pred())
    {
        const int rc = pthread_cond_timedwait(&_cond, &_lock, &deadline);
        if (rc == ETIMEDOUT)
        {
            return ETIMEDOUT;
        }
    }
    return 0;
}

// ---- EventObject ---------------------------------------------------------

EventObject::EventObject(bool manualReset, bool initialState)
    : WaitableObject(Kind),
      _signaled(initialState),
      _manualReset(manualReset)
{
}

DWORD EventObject::Wait(DWORD dwMilliseconds)
{
    pthread_mutex_lock(&_lock);
    const int rc = WaitWhile(dwMilliseconds, [this]{ return !_signaled; });
    DWORD ret = WAIT_OBJECT_0;
    if (rc == ETIMEDOUT)
    {
        ret = WAIT_TIMEOUT;
    }
    else if (!_manualReset)
    {
        _signaled = false;
    }
    pthread_mutex_unlock(&_lock);
    return ret;
}

void EventObject::Set()
{
    pthread_mutex_lock(&_lock);
    _signaled = true;
    if (_manualReset)
    {
        pthread_cond_broadcast(&_cond);
    }
    else
    {
        pthread_cond_signal(&_cond);
    }
    pthread_mutex_unlock(&_lock);
}

void EventObject::Reset()
{
    pthread_mutex_lock(&_lock);
    _signaled = false;
    pthread_mutex_unlock(&_lock);
}

void EventObject::Pulse()
{
    // Legacy. Wake all waiters once; do NOT leave the event signaled.
    pthread_mutex_lock(&_lock);
    _signaled = true;
    pthread_cond_broadcast(&_cond);
    _signaled = false;
    pthread_mutex_unlock(&_lock);
}

// ---- MutexObject ---------------------------------------------------------

MutexObject::MutexObject(bool initialOwner)
    : WaitableObject(Kind),
      _abandoned(false),
      _held(initialOwner),
      _recursion(initialOwner ? 1 : 0)
{
    if (initialOwner)
    {
        _owner = pthread_self();
    }
}

DWORD MutexObject::Wait(DWORD dwMilliseconds)
{
    pthread_mutex_lock(&_lock);
    const pthread_t self = pthread_self();
    if (_held && pthread_equal(_owner, self))
    {
        ++_recursion;
        pthread_mutex_unlock(&_lock);
        return WAIT_OBJECT_0;
    }
    const int rc = WaitWhile(dwMilliseconds, [this]{ return _held; });
    DWORD ret = WAIT_OBJECT_0;
    if (rc == ETIMEDOUT)
    {
        ret = WAIT_TIMEOUT;
    }
    else
    {
        _held = true;
        _owner = self;
        _recursion = 1;
        if (_abandoned)
        {
            _abandoned = false;
            ret = WAIT_ABANDONED;
        }
    }
    pthread_mutex_unlock(&_lock);
    return ret;
}

bool MutexObject::Release()
{
    pthread_mutex_lock(&_lock);
    if (!_held || !pthread_equal(_owner, pthread_self()))
    {
        pthread_mutex_unlock(&_lock);
        return false;
    }
    if (--_recursion == 0)
    {
        _held = false;
        pthread_cond_signal(&_cond);
    }
    pthread_mutex_unlock(&_lock);
    return true;
}

// ---- SemaphoreObject -----------------------------------------------------

SemaphoreObject::SemaphoreObject(LONG initialCount, LONG maximumCount)
    : WaitableObject(Kind),
      _current(initialCount),
      _max(maximumCount)
{
}

DWORD SemaphoreObject::Wait(DWORD dwMilliseconds)
{
    pthread_mutex_lock(&_lock);
    const int rc = WaitWhile(dwMilliseconds, [this]{ return _current <= 0; });
    DWORD ret = WAIT_OBJECT_0;
    if (rc == ETIMEDOUT)
    {
        ret = WAIT_TIMEOUT;
    }
    else
    {
        --_current;
    }
    pthread_mutex_unlock(&_lock);
    return ret;
}

bool SemaphoreObject::Release(LONG releaseCount, LPLONG previousCount)
{
    pthread_mutex_lock(&_lock);
    if (previousCount)
    {
        *previousCount = _current;
    }
    if (_current + releaseCount > _max)
    {
        pthread_mutex_unlock(&_lock);
        return false;
    }
    _current += releaseCount;
    for (LONG i = 0; i < releaseCount; ++i)
    {
        pthread_cond_signal(&_cond);
    }
    pthread_mutex_unlock(&_lock);
    return true;
}

// ---- ThreadObject --------------------------------------------------------

ThreadObject::ThreadObject(LPTHREAD_START_ROUTINE start, LPVOID param, bool suspended)
    : WaitableObject(Kind),
      _exitCode(STILL_ACTIVE),
      _finished(false),
      _start(start),
      _param(param),
      _suspended(suspended),
      _priority(THREAD_PRIORITY_NORMAL),
      _priorityBoostDisabled(false)
{
}

bool ThreadObject::Start(SIZE_T stackSize)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stackSize != 0)
    {
        pthread_attr_setstacksize(&attr, stackSize);
    }
    const int rc = pthread_create(&_thread, &attr, &ThreadObject::Trampoline, this);
    pthread_attr_destroy(&attr);
    return rc == 0;
}

void* ThreadObject::Trampoline(void* arg)
{
    static_cast<ThreadObject*>(arg)->Run();
    return nullptr;
}

void ThreadObject::Run()
{
    // CREATE_SUSPENDED: hold here until Resume() broadcasts.
    pthread_mutex_lock(&_lock);
    while (_suspended)
    {
        pthread_cond_wait(&_cond, &_lock);
    }
    const auto start = _start;
    const auto param = _param;
    pthread_mutex_unlock(&_lock);

    const DWORD rc = start(param);

    pthread_mutex_lock(&_lock);
    _exitCode = rc;
    _finished = true;
    pthread_cond_broadcast(&_cond);
    pthread_mutex_unlock(&_lock);
}

DWORD ThreadObject::Wait(DWORD dwMilliseconds)
{
    pthread_mutex_lock(&_lock);
    const int rc = WaitWhile(dwMilliseconds, [this]{ return !_finished; });
    const DWORD ret = (rc == ETIMEDOUT) ? WAIT_TIMEOUT : WAIT_OBJECT_0;
    pthread_mutex_unlock(&_lock);
    return ret;
}

DWORD ThreadObject::Resume()
{
    pthread_mutex_lock(&_lock);
    const DWORD prior = _suspended ? 1 : 0;
    if (_suspended)
    {
        _suspended = false;
        pthread_cond_broadcast(&_cond);
    }
    pthread_mutex_unlock(&_lock);
    return prior;
}

void ThreadObject::Terminate(DWORD exitCode)
{
    pthread_cancel(_thread);
    pthread_mutex_lock(&_lock);
    _exitCode = exitCode;
    _finished = true;
    pthread_cond_broadcast(&_cond);
    pthread_mutex_unlock(&_lock);
}

DWORD ThreadObject::ExitCode()
{
    pthread_mutex_lock(&_lock);
    const DWORD code = _finished ? _exitCode : STILL_ACTIVE;
    pthread_mutex_unlock(&_lock);
    return code;
}

// ---- FileObject ----------------------------------------------------------

FileObject::FileObject(int fd, DWORD flagsAndAttrs, DWORD desiredAccess,
    const char* path, bool deleteOnClose)
    : KObject(Kind),
      _fileFd(fd),
      _fileFlagsAndAttrs(flagsAndAttrs),
      _fileDesiredAccess(desiredAccess),
      _fileDeleteOnClosePath(deleteOnClose && path ? strdup(path) : nullptr),
      _fileOpenedPath(path ? strdup(path) : nullptr),
      _fsMagic(0)
{
}

FileObject::~FileObject()
{
    if (_fileFd >= 0)
    {
        close(_fileFd);
    }
    if (_fileDeleteOnClosePath)
    {
        unlink(_fileDeleteOnClosePath);
        free(_fileDeleteOnClosePath);
    }
    free(_fileOpenedPath);
}

void FileObject::ReplaceOpenedPath(char* newOwnedPath)
{
    free(_fileOpenedPath);
    _fileOpenedPath = newOwnedPath;
}

void FileObject::MarkDeleteOnClose()
{
    free(_fileDeleteOnClosePath);
    _fileDeleteOnClosePath = _fileOpenedPath ? strdup(_fileOpenedPath) : nullptr;
}

// ---- FindFileObject ------------------------------------------------------

FindFileObject::FindFileObject(void* dir, char* baseDir, char* pattern)
    : KObject(Kind),
      _findDir(dir),
      _findBaseDir(baseDir),
      _findPattern(pattern)
{
}

FindFileObject::~FindFileObject()
{
    if (_findDir)
    {
        closedir(static_cast<DIR*>(_findDir));
    }
    free(_findBaseDir);
    free(_findPattern);
}

// ---- FindVolumeObject ----------------------------------------------------

FindVolumeObject::FindVolumeObject()
    : KObject(Kind),
      _findVolumeIndex(0)
{
}

// ---- FileMappingObject ---------------------------------------------------

FileMappingObject::FileMappingObject(int fd, DWORD protect, QWORD maxSize)
    : KObject(Kind),
      _mappingFd(fd),
      _mappingProtect(protect),
      _mappingMaxSize(maxSize)
{
}

FileMappingObject::~FileMappingObject()
{
    if (_mappingFd >= 0)
    {
        close(_mappingFd);
    }
}

// ---- BlockDeviceObject ---------------------------------------------------

BlockDeviceObject::BlockDeviceObject(unsigned int major, unsigned int minor,
    unsigned int diskMajor, unsigned int diskMinor, char* diskName)
    : KObject(Kind),
      _blockMajor(major),
      _blockMinor(minor),
      _blockDiskMajor(diskMajor),
      _blockDiskMinor(diskMinor),
      _blockDiskName(diskName)
{
}

BlockDeviceObject::~BlockDeviceObject()
{
    free(_blockDiskName);
}

} // namespace osposix
