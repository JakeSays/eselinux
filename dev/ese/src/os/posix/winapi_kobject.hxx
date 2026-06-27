// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Internal: kernel-object backing for the Win32 HANDLE surface. KObject is
// the reference-counted base; each Win32 object kind (event, mutex, file,
// ...) is its own derived class carrying only its own state. Used by
// winapi_kobject.cxx (lifecycle + wait), winapi_sync.cxx, winapi_thread.cxx,
// and the file/find/mapping/blockdev surface. Not exported outside the
// posix layer.
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
    // synthesized for \\?\Volume{...} and \\.\PHYSICALDRIVE{N}
    BlockDevice,
};

// Reference-counted base for every HANDLE. CloseHandle calls Dereference and
// deletes at zero; DuplicateHandle calls Reference. Wait() is overridden by the
// waitable kinds; the default fails (file/find/mapping/blockdev handles are
// not waitable).
class KObject
{
public:
    explicit KObject(HandleKind kind);
    virtual ~KObject();

    HandleKind Kind() const
    {
        return _kind;
    }

    void Reference();

    // Drops a reference. Returns true when it was the last one — the caller
    // then deletes the object.
    bool Dereference();

    // Returns WAIT_OBJECT_0 / WAIT_TIMEOUT / WAIT_ABANDONED / WAIT_FAILED.
    // Acquires the object on success (auto-reset event clears, mutex takes
    // ownership, semaphore decrements). Caller must NOT hold the lock.
    virtual DWORD Wait(DWORD dwMilliseconds);

private:
    HandleKind _kind;
    std::atomic<int> _refCount;
};

// Base for the kinds that block: owns the pthread mutex + condition variable
// and the shared deadline-bounded wait helper.
class WaitableObject : public KObject
{
public:
    explicit WaitableObject(HandleKind kind);
    ~WaitableObject() override;

protected:
    // Waits while pred() holds (the wake condition is not yet met), honoring
    // an optional monotonic deadline. Returns 0 on satisfaction, ETIMEDOUT on
    // timeout. Caller must hold _lock.
    template<class Pred>
    int WaitWhile(DWORD dwMilliseconds, Pred pred);

    pthread_mutex_t _lock;
    pthread_cond_t _cond;
};

class EventObject : public WaitableObject
{
public:
    static constexpr HandleKind Kind = HandleKind::Event;

    EventObject(bool manualReset, bool initialState);

    DWORD Wait(DWORD dwMilliseconds) override;
    void Set();
    void Reset();
    void Pulse();

private:
    bool _signaled;
    bool _manualReset;
};

class MutexObject : public WaitableObject
{
public:
    static constexpr HandleKind Kind = HandleKind::Mutex;

    explicit MutexObject(bool initialOwner);

    DWORD Wait(DWORD dwMilliseconds) override;
    // Releases one level of ownership. Returns false if the caller doesn't
    // own the mutex.
    bool Release();

private:
    bool _abandoned;
    // Valid only when _held == true.
    pthread_t _owner;
    bool _held;
    int _recursion;
};

class SemaphoreObject : public WaitableObject
{
public:
    static constexpr HandleKind Kind = HandleKind::Semaphore;

    SemaphoreObject(LONG initialCount, LONG maximumCount);

    DWORD Wait(DWORD dwMilliseconds) override;
    // Raises the count by releaseCount. Returns false (and changes nothing)
    // if that would exceed the maximum.
    bool Release(LONG releaseCount, LPLONG previousCount);

private:
    LONG _current;
    LONG _max;
};

class ThreadObject : public WaitableObject
{
public:
    static constexpr HandleKind Kind = HandleKind::Thread;

    ThreadObject(LPTHREAD_START_ROUTINE start, LPVOID param, bool suspended);

    // Spawns the worker (honoring CREATE_SUSPENDED). Returns false if the
    // pthread couldn't be created.
    bool Start(SIZE_T stackSize);

    DWORD Wait(DWORD dwMilliseconds) override;
    // Lifts a CREATE_SUSPENDED hold. Returns the prior suspend count (0 or 1).
    DWORD Resume();
    // Forced cancellation; records exitCode and marks the thread finished.
    void Terminate(DWORD exitCode);
    // exitCode once finished, STILL_ACTIVE before.
    DWORD ExitCode();

    int Priority() const
    {
        return _priority;
    }
    void SetPriority(int priority)
    {
        _priority = priority;
    }
    bool PriorityBoostDisabled() const
    {
        return _priorityBoostDisabled;
    }
    void SetPriorityBoostDisabled(bool disabled)
    {
        _priorityBoostDisabled = disabled;
    }

private:
    // pthread entry trampoline — forwards to Run().
    static void* Trampoline(void* arg);
    // The worker body: honor the suspend barrier, invoke the user proc,
    // record completion.
    void Run();

    pthread_t _thread;
    DWORD _exitCode;
    bool _finished;
    LPTHREAD_START_ROUTINE _start;
    LPVOID _param;
    bool _suspended;
    int _priority;
    bool _priorityBoostDisabled;
};

class FileObject : public KObject
{
public:
    static constexpr HandleKind Kind = HandleKind::File;

    // Takes ownership of the OS fd. Copies path internally; if deleteOnClose
    // the path is unlinked when the handle closes.
    FileObject(int fd, DWORD flagsAndAttrs, DWORD desiredAccess,
        const char* path, bool deleteOnClose);
    ~FileObject() override;

    int Fd() const
    {
        return _fileFd;
    }
    DWORD FlagsAndAttrs() const
    {
        return _fileFlagsAndAttrs;
    }
    DWORD DesiredAccess() const
    {
        return _fileDesiredAccess;
    }
    const char* OpenedPath() const
    {
        return _fileOpenedPath;
    }
    // Replaces the opened-path (used by rename); frees the old one and takes
    // ownership of newOwnedPath.
    void ReplaceOpenedPath(char* newOwnedPath);
    // Marks the file for unlink-on-close at its current opened path.
    void MarkDeleteOnClose();
    // statfs f_type recorded at open; 0 until the filesystem is probed.
    unsigned long FsMagic() const
    {
        return _fsMagic;
    }
    void SetFsMagic(unsigned long magic)
    {
        _fsMagic = magic;
    }

private:
    int _fileFd;
    DWORD _fileFlagsAndAttrs;
    DWORD _fileDesiredAccess;
    // Owned; non-null when delete-on-close was requested.
    char* _fileDeleteOnClosePath;
    // Owned narrow UTF-8.
    char* _fileOpenedPath;
    unsigned long _fsMagic;
};

class FindFileObject : public KObject
{
public:
    static constexpr HandleKind Kind = HandleKind::FindFile;

    // Takes ownership of the DIR* and both heap strings.
    FindFileObject(void* dir, char* baseDir, char* pattern);
    ~FindFileObject() override;

    // DIR* — opendir handle.
    void* Dir() const
    {
        return _findDir;
    }
    const char* BaseDir() const
    {
        return _findBaseDir;
    }
    const char* Pattern() const
    {
        return _findPattern;
    }

private:
    void* _findDir;
    char* _findBaseDir;
    char* _findPattern;
};

class FindVolumeObject : public KObject
{
public:
    static constexpr HandleKind Kind = HandleKind::FindVolume;

    FindVolumeObject();

    int Cursor() const
    {
        return _findVolumeIndex;
    }
    void SetCursor(int index)
    {
        _findVolumeIndex = index;
    }

private:
    // FindNextVolumeW iteration cursor.
    int _findVolumeIndex;
};

class FileMappingObject : public KObject
{
public:
    static constexpr HandleKind Kind = HandleKind::FileMapping;

    // Takes ownership of the dup'd fd.
    FileMappingObject(int fd, DWORD protect, QWORD maxSize);
    ~FileMappingObject() override;

    // Dup'd file fd; closed when the mapping handle is closed.
    int Fd() const
    {
        return _mappingFd;
    }
    // PAGE_READONLY / PAGE_READWRITE (translated to mmap PROT).
    DWORD Protect() const
    {
        return _mappingProtect;
    }
    // Max view size; 0 means "to end of file".
    QWORD MaxSize() const
    {
        return _mappingMaxSize;
    }

private:
    int _mappingFd;
    DWORD _mappingProtect;
    QWORD _mappingMaxSize;
};

// Synthesized handle for CreateFileW on \\?\Volume{X-Y} or
// \\.\PHYSICALDRIVE{N}. Carries the (major,minor) of the underlying
// filesystem or whole-disk device plus the kernel device name (e.g. "sda1"
// or "sda" — no /dev/ prefix). DeviceIoControl uses these to answer
// IOCTL_STORAGE_QUERY_PROPERTY / IOCTL_DISK_GET_CACHE_INFORMATION /
// IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS by reading /sys/block/<name>/... No
// real fd is held — the kernel name suffices for sysfs lookups.
class BlockDeviceObject : public KObject
{
public:
    static constexpr HandleKind Kind = HandleKind::BlockDevice;

    // Takes ownership of diskName (heap, narrow UTF-8, no /dev/ prefix; null
    // means "no sysfs backing").
    BlockDeviceObject(unsigned int major, unsigned int minor,
        unsigned int diskMajor, unsigned int diskMinor, char* diskName);
    ~BlockDeviceObject() override;

    unsigned int Major() const
    {
        return _blockMajor;
    }
    unsigned int Minor() const
    {
        return _blockMinor;
    }
    // Whole-disk parent — equals Major()/Minor() if already a whole disk.
    unsigned int DiskMajor() const
    {
        return _blockDiskMajor;
    }
    unsigned int DiskMinor() const
    {
        return _blockDiskMinor;
    }
    // Owned narrow UTF-8, no /dev/ prefix (e.g. "sda"); null if no backing.
    const char* DiskName() const
    {
        return _blockDiskName;
    }

private:
    unsigned int _blockMajor;
    unsigned int _blockMinor;
    unsigned int _blockDiskMajor;
    unsigned int _blockDiskMinor;
    char* _blockDiskName;
};

inline KObject* HandleToK(HANDLE h)
{
    return static_cast<KObject*>(h);
}

inline HANDLE KToHandle(KObject* k)
{
    return k;
}

// Checked downcast: returns the object as T* only when its kind matches,
// nullptr otherwise. Mirrors the old HandleToK + explicit kind check.
template<class T>
inline T* As(KObject* k)
{
    return (k && k->Kind() == T::Kind)
        ? static_cast<T*>(k)
        : nullptr;
}

template<class T>
inline T* As(HANDLE h)
{
    return As<T>(HandleToK(h));
}

// Monotonic-deadline helpers shared by the wait paths (WaitableObject::Wait
// overrides and WaitForMultipleObjectsEx's poll loop).
void GetMonotonicDeadline(DWORD dwMilliseconds, struct timespec* deadline);

bool DeadlinePassed(const struct timespec& deadline);

// Block-device synthesis: see winapi_blockdev.cxx.
bool ResolveBlockDeviceForPath(const char* path,
    unsigned int* pFsMajor, unsigned int* pFsMinor,
    unsigned int* pDiskMajor, unsigned int* pDiskMinor,
    char* diskName, size_t cchName);
} // namespace osposix
