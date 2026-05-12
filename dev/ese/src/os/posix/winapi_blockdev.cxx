// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Block-device identity for the Linux port.
//
// The engine learns about physical storage through three Win32 abstractions:
//
//   * \\?\Volume{GUID}\        — a "volume" handle (one per filesystem mount);
//                                source of canonical sector geometry and the
//                                disk-extents query that maps volume → disk.
//   * \\.\PHYSICALDRIVE{N}     — a "physical drive" handle (one per whole
//                                disk); source of cache info, vendor/model,
//                                seek-penalty, TRIM support, etc.
//   * IFileAPI handle on a regular file — same disk inference happens
//                                transparently through st_dev.
//
// We map all three onto a `HandleKind::BlockDevice` KObject carrying
// (major:minor) of the filesystem + (major:minor) of the whole-disk parent
// + the kernel device name (e.g. "sda" or "sda1") for sysfs lookups.  No
// real fd is held — sysfs reads are by-name.  IOCTL queries dispatch to
// the helpers below.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include "winapi_path.hxx"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace osposix
{

namespace
{

//  Read a small ASCII file (sysfs entry) into a caller-owned buffer.
//  Returns the byte count read (0 on failure), null-terminating the buffer.
size_t ReadSysfsFile(const char* path, char* buf, size_t cbBuf)
{
    if (cbBuf == 0)
        return 0;
    buf[0] = '\0';
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t n;
    do
    {
        n = read(fd, buf, cbBuf - 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    if (n <= 0)
        return 0;
    //  Strip trailing newline.
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        --n;
    buf[n] = '\0';
    return (size_t) n;
}

//  Strip a "/sys/dev/block/X:Y" -> ".../<diskname>/<partname>" symlink
//  result down to the disk basename.  E.g.,
//    /sys/devices/pci.../host0/.../sda/sda1   -> sda1
//  When called with the disk-itself link it yields the disk basename.
const char* BasenameOf(const char* path)
{
    const char* last = strrchr(path, '/');
    return last ? last + 1 : path;
}

//  Read /sys/dev/block/<major>:<minor>/{stat,partition,uevent} to discover
//  the parent whole-disk device.  Sets *pParentMajor / *pParentMinor /
//  diskName.  Returns true on success.
bool ResolveDisk(unsigned int major, unsigned int minor,
                 unsigned int* pParentMajor, unsigned int* pParentMinor,
                 char* diskName, size_t cchName)
{
    char link[PATH_MAX];
    snprintf(link, sizeof(link), "/sys/dev/block/%u:%u", major, minor);
    char real[PATH_MAX];
    if (realpath(link, real) == nullptr)
        return false;
    //  real now looks like /sys/devices/.../sda or /sys/devices/.../sda/sda1.
    //  Determine which by reading /sys/dev/block/X:Y/partition — present
    //  for partitions, absent for whole-disk nodes.
    char partFile[PATH_MAX];
    snprintf(partFile, sizeof(partFile), "%s/partition", real);
    struct stat st;
    char diskDir[PATH_MAX];
    if (stat(partFile, &st) == 0)
    {
        //  Partition — walk up one directory.
        size_t len = strlen(real);
        const char* slash = strrchr(real, '/');
        if (!slash)
            return false;
        const size_t prefixLen = (size_t)(slash - real);
        if (prefixLen + 1 > sizeof(diskDir))
            return false;
        memcpy(diskDir, real, prefixLen);
        diskDir[prefixLen] = '\0';
    }
    else
    {
        //  Whole disk already.
        if (strlen(real) + 1 > sizeof(diskDir))
            return false;
        memcpy(diskDir, real, strlen(real) + 1);
    }

    //  Disk name is the basename of diskDir.
    const char* name = BasenameOf(diskDir);
    if (strlen(name) + 1 > cchName)
        return false;
    memcpy(diskName, name, strlen(name) + 1);

    //  Read /sys/block/<name>/dev for the disk's (major:minor).
    char devFile[PATH_MAX];
    snprintf(devFile, sizeof(devFile), "/sys/block/%s/dev", diskName);
    char devBuf[64];
    if (ReadSysfsFile(devFile, devBuf, sizeof(devBuf)) == 0)
    {
        *pParentMajor = major;
        *pParentMinor = minor;
        return true;
    }
    unsigned int pMaj = 0, pMin = 0;
    if (sscanf(devBuf, "%u:%u", &pMaj, &pMin) != 2)
    {
        *pParentMajor = major;
        *pParentMinor = minor;
        return true;
    }
    *pParentMajor = pMaj;
    *pParentMinor = pMin;
    return true;
}

}  //  namespace

//  Public: stat(path) → resolve to (fs major:minor) + (disk major:minor) +
//  disk name.  Returns false if any step fails; caller should fall back to
//  ERROR_INVALID_FUNCTION semantics in that case.
bool ResolveBlockDeviceForPath(const char* path,
                               unsigned int* pFsMajor, unsigned int* pFsMinor,
                               unsigned int* pDiskMajor, unsigned int* pDiskMinor,
                               char* diskName, size_t cchName)
{
    if (!path || !pFsMajor || !pFsMinor || !pDiskMajor || !pDiskMinor || !diskName || cchName == 0)
        return false;
    struct stat st;
    if (stat(path, &st) < 0)
        return false;
    const unsigned int fsMaj = major(st.st_dev);
    const unsigned int fsMin = minor(st.st_dev);
    if (!ResolveDisk(fsMaj, fsMin, pDiskMajor, pDiskMinor, diskName, cchName))
    {
        //  Filesystem isn't backed by a /sys/dev/block entry (tmpfs, overlayfs,
        //  NFS, autofs, ...).  Synthesize a stable but unresolvable identity:
        //  emit the (major:minor) as the disk identifier and use a synthetic
        //  name so DeviceIoControl can detect the missing real disk.
        *pDiskMajor = fsMaj;
        *pDiskMinor = fsMin;
        if (cchName < 1)
            return false;
        diskName[0] = '\0';
    }
    *pFsMajor = fsMaj;
    *pFsMinor = fsMin;
    return true;
}

//  Public: create a synthetic BlockDevice KObject.  Caller transfers
//  ownership of diskName (a heap-allocated, narrow UTF-8 string with no
//  /dev/ prefix; null means "no sysfs backing").
KObject* AllocBlockDeviceKObject(unsigned int fsMajor, unsigned int fsMinor,
                                 unsigned int diskMajor, unsigned int diskMinor,
                                 char* diskName)
{
    KObject* const k = AllocKObject(HandleKind::BlockDevice);
    if (!k)
    {
        free(diskName);
        return nullptr;
    }
    k->blockMajor      = fsMajor;
    k->blockMinor      = fsMinor;
    k->blockDiskMajor  = diskMajor;
    k->blockDiskMinor  = diskMinor;
    k->blockDiskName   = diskName;
    return k;
}

}  //  namespace osposix
