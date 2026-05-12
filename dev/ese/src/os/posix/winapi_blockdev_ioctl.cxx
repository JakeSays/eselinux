// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// DeviceIoControl handlers for the block-device storage queries:
//
//   IOCTL_STORAGE_QUERY_PROPERTY          (sub-properties below)
//   IOCTL_DISK_GET_CACHE_INFORMATION
//   IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS
//
// Backing data comes from /sys/block/<name>/... — see ResolveBlockDeviceFor*
// in winapi_blockdev.cxx for the (path|fd) -> disk-name resolution.

#include "osstd.hxx"
#include "winapi_kobject.hxx"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

//  windows-shim/specstrings.h #defines __reserved → empty as a SAL annotation.
//  <linux/fs.h> transitively pulls fscrypt.h, which has a struct field named
//  __reserved.  Drop the macro just before the kernel headers.
#undef __reserved
#include <linux/fs.h>

using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;

namespace
{

//  Read a small ASCII file (sysfs entry) into a caller-owned buffer.
size_t ReadSmallFile(const char* path, char* buf, size_t cbBuf)
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
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
        --n;
    buf[n] = '\0';
    return (size_t) n;
}

unsigned long ReadSysfsULong(const char* path, unsigned long defValue)
{
    char buf[64];
    if (ReadSmallFile(path, buf, sizeof(buf)) == 0)
        return defValue;
    char* end = nullptr;
    const unsigned long v = strtoul(buf, &end, 10);
    return (end == buf) ? defValue : v;
}

//  Map a KObject (BlockDevice or File on a regular file) to a disk name
//  suitable for /sys/block lookups.  Returns true if we have a usable name.
bool DiskNameForHandle(KObject* k, char* nameOut, size_t cchNameOut,
                       unsigned int* pDiskMajor, unsigned int* pDiskMinor)
{
    if (k->kind == HandleKind::BlockDevice)
    {
        if (pDiskMajor) *pDiskMajor = k->blockDiskMajor;
        if (pDiskMinor) *pDiskMinor = k->blockDiskMinor;
        if (!k->blockDiskName || !*k->blockDiskName)
            return false;
        const size_t n = strlen(k->blockDiskName);
        if (n + 1 > cchNameOut)
            return false;
        memcpy(nameOut, k->blockDiskName, n + 1);
        return true;
    }
    if (k->kind == HandleKind::File && k->fileFd >= 0)
    {
        //  Resolve the disk-name backing this regular file.
        struct stat st;
        if (fstat(k->fileFd, &st) < 0)
            return false;
        unsigned int dMaj = 0, dMin = 0;
        char tmpName[64];
        if (!osposix::ResolveBlockDeviceForPath(k->fileOpenedPath ? k->fileOpenedPath : "/",
                                                &dMaj, &dMin, &dMaj, &dMin,
                                                tmpName, sizeof(tmpName))
            || tmpName[0] == '\0')
        {
            return false;
        }
        if (pDiskMajor) *pDiskMajor = dMaj;
        if (pDiskMinor) *pDiskMinor = dMin;
        const size_t n = strlen(tmpName);
        if (n + 1 > cchNameOut)
            return false;
        memcpy(nameOut, tmpName, n + 1);
        return true;
    }
    return false;
}

//  When `diskName` is empty (filesystems backed by an anonymous block dev:
//  ZFS dataset, tmpfs, NFS, fuse, overlay, etc.) sysfs has no hardware
//  info to report and these helpers fall back to conservative defaults:
//  4096/4096 sector sizes, no seek penalty (assume SSD-class behaviour
//  since flash is the common case for non-block-backed mounts), no TRIM.

constexpr DWORD c_defaultLogicalSectorBytes  = 512;
constexpr DWORD c_defaultPhysicalSectorBytes = 4096;

BOOL FillSectorAlignmentDescriptor(const char* diskName,
                                   STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR* p)
{
    memset(p, 0, sizeof(*p));
    p->Version = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
    p->Size    = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
    if (diskName && *diskName)
    {
        char path[256];
        snprintf(path, sizeof(path), "/sys/block/%s/queue/logical_block_size", diskName);
        p->BytesPerLogicalSector = (DWORD) ReadSysfsULong(path, c_defaultLogicalSectorBytes);
        snprintf(path, sizeof(path), "/sys/block/%s/queue/physical_block_size", diskName);
        p->BytesPerPhysicalSector = (DWORD) ReadSysfsULong(path, c_defaultPhysicalSectorBytes);
        snprintf(path, sizeof(path), "/sys/block/%s/queue/io_min", diskName);
        p->BytesPerCacheLine = (DWORD) ReadSysfsULong(path, p->BytesPerPhysicalSector);
    }
    else
    {
        p->BytesPerLogicalSector  = c_defaultPhysicalSectorBytes;
        p->BytesPerPhysicalSector = c_defaultPhysicalSectorBytes;
        p->BytesPerCacheLine      = c_defaultPhysicalSectorBytes;
    }
    return TRUE;
}

BOOL FillSeekPenaltyDescriptor(const char* diskName, DEVICE_SEEK_PENALTY_DESCRIPTOR* p)
{
    memset(p, 0, sizeof(*p));
    p->Version = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);
    p->Size    = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);
    if (diskName && *diskName)
    {
        char path[256];
        snprintf(path, sizeof(path), "/sys/block/%s/queue/rotational", diskName);
        p->IncursSeekPenalty = ReadSysfsULong(path, 1) != 0;
    }
    else
    {
        //  Virtual filesystems (ZFS/tmpfs/etc.) have no rotational seek;
        //  ZFS in particular caches in ARC and writes async — the engine's
        //  SSD-flavoured codepaths are a better fit than the HDD ones.
        p->IncursSeekPenalty = FALSE;
    }
    return TRUE;
}

BOOL FillTrimDescriptor(const char* diskName, DEVICE_TRIM_DESCRIPTOR* p)
{
    memset(p, 0, sizeof(*p));
    p->Version = sizeof(DEVICE_TRIM_DESCRIPTOR);
    p->Size    = sizeof(DEVICE_TRIM_DESCRIPTOR);
    if (diskName && *diskName)
    {
        char path[256];
        snprintf(path, sizeof(path), "/sys/block/%s/queue/discard_max_bytes", diskName);
        p->TrimEnabled = ReadSysfsULong(path, 0) > 0;
    }
    else
    {
        //  No hardware TRIM concept for ZFS/tmpfs; engine treats absent
        //  TRIM as "punch-hole via fallocate" which we already shim.
        p->TrimEnabled = FALSE;
    }
    return TRUE;
}

BOOL FillDeviceDescriptor(const char* diskName,
                          STORAGE_DEVICE_DESCRIPTOR* p, DWORD cbBuf, DWORD* pcbReturned)
{
    if (cbBuf < sizeof(STORAGE_DEVICE_DESCRIPTOR))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memset(p, 0, sizeof(*p));
    p->Version = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    p->Size    = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    p->DeviceType         = 0x07;   //  FILE_DEVICE_MASS_STORAGE
    p->RemovableMedia     = FALSE;
    p->CommandQueueing    = TRUE;
    p->VendorIdOffset     = 0;
    p->ProductIdOffset    = 0;
    p->ProductRevisionOffset = 0;
    p->SerialNumberOffset = 0;
    p->BusType            = BusTypeUnknown;

    char path[256];
    snprintf(path, sizeof(path), "/sys/block/%s/device/vendor", diskName);
    char vendor[64];
    const size_t cbVendor = ReadSmallFile(path, vendor, sizeof(vendor));
    snprintf(path, sizeof(path), "/sys/block/%s/device/model", diskName);
    char model[64];
    const size_t cbModel = ReadSmallFile(path, model, sizeof(model));
    //  SCSI/SATA expose firmware as .../device/rev; NVMe uses firmware_rev.
    snprintf(path, sizeof(path), "/sys/block/%s/device/rev", diskName);
    char firmware[64];
    size_t cbFirmware = ReadSmallFile(path, firmware, sizeof(firmware));
    if (cbFirmware == 0)
    {
        snprintf(path, sizeof(path), "/sys/block/%s/device/firmware_rev", diskName);
        cbFirmware = ReadSmallFile(path, firmware, sizeof(firmware));
    }
    //  Serial varies by transport: NVMe at .../device/serial, SCSI at
    //  .../device/vpd_pg80 (binary, root-only), SATA usually nowhere.
    //  Best-effort: just try /sys/block/<name>/device/serial.
    snprintf(path, sizeof(path), "/sys/block/%s/device/serial", diskName);
    char serial[64];
    const size_t cbSerial = ReadSmallFile(path, serial, sizeof(serial));

    //  Pack strings after the fixed-size header.  Win32 contract: each
    //  *Offset field is relative to the start of the descriptor.
    BYTE* const base = reinterpret_cast<BYTE*>(p);
    DWORD ib = sizeof(STORAGE_DEVICE_DESCRIPTOR);
    auto Pack = [&](const char* sz, size_t cb, DWORD& offsetOut) -> bool
    {
        if (cb == 0)
        {
            offsetOut = 0;
            return true;
        }
        if (ib + cb + 1 > cbBuf)
            return false;
        offsetOut = ib;
        memcpy(base + ib, sz, cb);
        base[ib + cb] = '\0';
        ib += (DWORD)(cb + 1);
        return true;
    };
    if (!Pack(vendor,   cbVendor,   p->VendorIdOffset)
        || !Pack(model, cbModel,    p->ProductIdOffset)
        || !Pack(firmware, cbFirmware, p->ProductRevisionOffset)
        || !Pack(serial, cbSerial,  p->SerialNumberOffset))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    p->RawPropertiesLength = ib - sizeof(STORAGE_DEVICE_DESCRIPTOR);
    if (pcbReturned)
        *pcbReturned = ib;
    return TRUE;
}

BOOL FillAdapterDescriptor(const char* diskName, STORAGE_ADAPTER_DESCRIPTOR* p)
{
    memset(p, 0, sizeof(*p));
    p->Version = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    p->Size    = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
    //  Linux block layer doesn't gate per-adapter; report generous defaults
    //  matching what a healthy SATA/NVMe controller advertises.
    p->MaximumTransferLength = 0x00100000u;
    p->MaximumPhysicalPages  = 256;
    p->AlignmentMask         = 0;
    p->AdapterUsesPio        = FALSE;
    p->AdapterScansDown      = FALSE;
    p->CommandQueueing       = TRUE;
    p->AcceleratedTransfer   = TRUE;
    p->BusType               = BusTypeUnknown;
    if (diskName && *diskName)
    {
        //  NVMe disks live under /sys/block/nvmeXnY/...; everything else is
        //  too transport-specific to detect from sysfs alone.
        if (strncmp(diskName, "nvme", 4) == 0)
            p->BusType = BusTypeNvme;
        else
            p->BusType = BusTypeSata;
    }
    return TRUE;
}

//  Read /proc/diskstats and return the in-flight IO count for the given
//  (major:minor).  Returns 0 when the device isn't found.
DWORD ReadDiskstatsInflight(unsigned int diskMajor, unsigned int diskMinor)
{
    FILE* const f = fopen("/proc/diskstats", "re");
    if (!f)
        return 0;
    char line[512];
    DWORD inflight = 0;
    while (fgets(line, sizeof(line), f))
    {
        unsigned int maj = 0, minr = 0;
        char name[64];
        //  /proc/diskstats columns (kernel 4.18+): major minor name
        //  rd_ios rd_merges rd_sectors rd_ticks wr_ios wr_merges wr_sectors
        //  wr_ticks in_flight io_ticks time_in_queue ...
        unsigned long long rdIos, rdMerges, rdSectors, rdTicks;
        unsigned long long wrIos, wrMerges, wrSectors, wrTicks;
        unsigned long long inFlight;
        const int n = sscanf(line,
                "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                &maj, &minr, name,
                &rdIos, &rdMerges, &rdSectors, &rdTicks,
                &wrIos, &wrMerges, &wrSectors, &wrTicks,
                &inFlight);
        if (n >= 12 && maj == diskMajor && minr == diskMinor)
        {
            inflight = (DWORD) inFlight;
            break;
        }
    }
    fclose(f);
    return inflight;
}

BOOL FillDiskPerformance(unsigned int diskMajor, unsigned int diskMinor,
                         DISK_PERFORMANCE* p)
{
    memset(p, 0, sizeof(*p));
    p->QueueDepth = ReadDiskstatsInflight(diskMajor, diskMinor);
    //  StorageDeviceNumber, StorageManagerName etc. left zero — engine reads
    //  QueueDepth only.
    return TRUE;
}

}  //  namespace

extern "C"
{

//  Public entry — called from winapi_filelock.cxx's DeviceIoControl
//  dispatcher.  Returns FALSE with ERROR_INVALID_FUNCTION when the
//  control code isn't a storage IOCTL we handle (caller continues
//  with the FSCTL_* switch).
BOOL OSPosixHandleStorageIoctl(HANDLE hDevice, DWORD dwIoControlCode,
    LPVOID lpInBuffer, DWORD nInBufferSize,
    LPVOID lpOutBuffer, DWORD nOutBufferSize, LPDWORD lpBytesReturned)
{
    KObject* const k = HandleToK(hDevice);
    if (!k)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    //  fHaveDisk == false just means "no sysfs backing for hardware
    //  queries" — ZFS, tmpfs, NFS, fuse, overlay all land here.  The
    //  IOCTL helpers below substitute sensible defaults; the engine's
    //  COSDisk grouping still works because (diskMajor:diskMinor)
    //  uniquely identifies the filesystem.
    char diskName[64] = "";
    unsigned int diskMajor = 0, diskMinor = 0;
    (void) DiskNameForHandle(k, diskName, sizeof(diskName),
                             &diskMajor, &diskMinor);

    if (dwIoControlCode == IOCTL_STORAGE_QUERY_PROPERTY)
    {
        if (nInBufferSize < sizeof(STORAGE_PROPERTY_QUERY) || !lpInBuffer || !lpOutBuffer)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        const STORAGE_PROPERTY_QUERY* const q =
                reinterpret_cast<const STORAGE_PROPERTY_QUERY*>(lpInBuffer);
        if (q->QueryType != PropertyStandardQuery && q->QueryType != PropertyExistsQuery)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        switch (q->PropertyId)
        {
            case StorageAccessAlignmentProperty:
                if (nOutBufferSize < sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                FillSectorAlignmentDescriptor(diskName,
                    reinterpret_cast<STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR*>(lpOutBuffer));
                if (lpBytesReturned)
                    *lpBytesReturned = sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR);
                return TRUE;

            case StorageDeviceSeekPenaltyProperty:
                if (nOutBufferSize < sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                FillSeekPenaltyDescriptor(diskName,
                    reinterpret_cast<DEVICE_SEEK_PENALTY_DESCRIPTOR*>(lpOutBuffer));
                if (lpBytesReturned)
                    *lpBytesReturned = sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR);
                return TRUE;

            case StorageDeviceTrimProperty:
                if (nOutBufferSize < sizeof(DEVICE_TRIM_DESCRIPTOR))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                FillTrimDescriptor(diskName,
                    reinterpret_cast<DEVICE_TRIM_DESCRIPTOR*>(lpOutBuffer));
                if (lpBytesReturned)
                    *lpBytesReturned = sizeof(DEVICE_TRIM_DESCRIPTOR);
                return TRUE;

            case StorageDeviceProperty:
            {
                DWORD cbReturned = 0;
                const BOOL ok = FillDeviceDescriptor(diskName,
                    reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(lpOutBuffer),
                    nOutBufferSize, &cbReturned);
                if (ok && lpBytesReturned)
                    *lpBytesReturned = cbReturned;
                return ok;
            }

            case StorageAdapterProperty:
                if (nOutBufferSize < sizeof(STORAGE_ADAPTER_DESCRIPTOR))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                FillAdapterDescriptor(diskName,
                    reinterpret_cast<STORAGE_ADAPTER_DESCRIPTOR*>(lpOutBuffer));
                if (lpBytesReturned)
                    *lpBytesReturned = sizeof(STORAGE_ADAPTER_DESCRIPTOR);
                return TRUE;

            case StorageDeviceCopyOffloadProperty:
                //  Linux has no token-based copy-offload abstraction (ODX is
                //  a Win32/SCSI-XCOPY thing).  Report not-supported; engine's
                //  m_errorOsdcod path handles the negative case.
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;

            case StorageDeviceWriteCacheProperty:
                //  IOCTL_DISK_GET_CACHE_INFORMATION already serves the
                //  write-cache state below; the engine treats a failure here
                //  as "use the IOCTL_DISK_GET_CACHE_INFORMATION answer."
                SetLastError(ERROR_NOT_SUPPORTED);
                return FALSE;
        }
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    //  SMART_GET_VERSION / SMART_RCV_DRIVE_DATA require sg_io plumbing and
    //  CAP_SYS_RAWIO on Linux.  Reporting not-supported lets the engine's
    //  SetSmartEseNoLoadFailed path record the reason and continue without
    //  ATA self-monitoring data.
    if (dwIoControlCode == SMART_GET_VERSION
        || dwIoControlCode == SMART_RCV_DRIVE_DATA)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }

    if (dwIoControlCode == IOCTL_DISK_PERFORMANCE)
    {
        if (nOutBufferSize < sizeof(DISK_PERFORMANCE))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        FillDiskPerformance(k->blockDiskMajor, k->blockDiskMinor,
            reinterpret_cast<DISK_PERFORMANCE*>(lpOutBuffer));
        if (lpBytesReturned)
            *lpBytesReturned = sizeof(DISK_PERFORMANCE);
        return TRUE;
    }

    if (dwIoControlCode == IOCTL_DISK_GET_CACHE_INFORMATION)
    {
        if (nOutBufferSize < sizeof(DISK_CACHE_INFORMATION))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        DISK_CACHE_INFORMATION* const p =
                reinterpret_cast<DISK_CACHE_INFORMATION*>(lpOutBuffer);
        memset(p, 0, sizeof(*p));
        static constexpr char c_writeBack[] = "write back";
        bool fWriteCacheBack = true;    //  default: assume write-back (matches Linux block-layer default)
        if (diskName[0])
        {
            char path[256];
            snprintf(path, sizeof(path), "/sys/block/%s/queue/write_cache", diskName);
            char buf[32];
            ReadSmallFile(path, buf, sizeof(buf));
            fWriteCacheBack = (strncmp(buf, c_writeBack, sizeof(c_writeBack) - 1) == 0);
        }
        p->WriteCacheEnabled = fWriteCacheBack;
        p->ReadCacheEnabled  = TRUE;    //  Linux page cache always on
        p->ParametersSavable = FALSE;
        p->ReadRetentionPriority  = KeepReadData;
        p->WriteRetentionPriority = KeepReadData;
        if (lpBytesReturned)
            *lpBytesReturned = sizeof(*p);
        return TRUE;
    }

    if (dwIoControlCode == IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS)
    {
        if (nOutBufferSize < sizeof(VOLUME_DISK_EXTENTS))
        {
            SetLastError(ERROR_MORE_DATA);
            if (lpBytesReturned)
                *lpBytesReturned = sizeof(VOLUME_DISK_EXTENTS);
            return FALSE;
        }
        VOLUME_DISK_EXTENTS* const p =
                reinterpret_cast<VOLUME_DISK_EXTENTS*>(lpOutBuffer);
        memset(p, 0, sizeof(*p));
        p->NumberOfDiskExtents       = 1;
        //  Encode the disk's (major:minor) into a unique DiskNumber.
        //  Engine treats this as an opaque identifier for COSDisk grouping.
        p->Extents[0].DiskNumber     = (DWORD) makedev(diskMajor, diskMinor) & 0x7FFFFFFFu;
        p->Extents[0].StartingOffset.QuadPart = 0;
        p->Extents[0].ExtentLength.QuadPart   = 0;
        if (lpBytesReturned)
            *lpBytesReturned = sizeof(*p);
        return TRUE;
    }

    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

}  //  extern "C"
