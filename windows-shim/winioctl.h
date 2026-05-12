// Linux shim for <winioctl.h>. Provides struct shapes the OS internal
// headers (_osdisk.hxx, _osfile.hxx) reference at parse time. The
// IOCTL_DISK_* / FSCTL_* runtime values are NOT translated to Linux
// equivalents here — that lives in os/posix/disk.cxx (Phase 5).
#pragma once

#include "winnt.h"

#ifdef __cplusplus
extern "C" {
#endif

//  Mirrors Windows DISK_CACHE_INFORMATION shape (see ntddstor.h). The
//  engine reads these fields for diagnostic logging only; the posix layer
//  fills sensible defaults.
//
typedef enum _DISK_CACHE_RETENTION_PRIORITY {
    EqualPriority   = 0,
    KeepPrefetchedData = 1,
    KeepReadData    = 2,
} DISK_CACHE_RETENTION_PRIORITY;

typedef struct _DISK_CACHE_INFORMATION {
    BOOLEAN ParametersSavable;
    BOOLEAN ReadCacheEnabled;
    BOOLEAN WriteCacheEnabled;
    DISK_CACHE_RETENTION_PRIORITY ReadRetentionPriority;
    DISK_CACHE_RETENTION_PRIORITY WriteRetentionPriority;
    WORD    DisablePrefetchTransferLength;
    BOOLEAN PrefetchScalar;
    union {
        struct {
            WORD Minimum;
            WORD Maximum;
            WORD MaximumBlocks;
        } ScalarPrefetch;
        struct {
            WORD Minimum;
            WORD Maximum;
        } BlockPrefetch;
    };
} DISK_CACHE_INFORMATION, *PDISK_CACHE_INFORMATION;

//  STORAGE_PROPERTY_QUERY / STORAGE_DEVICE_DESCRIPTOR shapes used by
//  COSDisk / IOREQ paths. Stubbed minimally; flesh out when the disk
//  query path is ported.
//
//  Storage descriptor structs referenced by COSDisk::OSDiskInfo. The engine
//  reads these for diagnostic logging only; posix layer fills sensible
//  defaults based on /sys/block/<dev>/queue/* on Linux.
//
//  STORAGE_PROPERTY_QUERY — IOCTL_STORAGE_QUERY_PROPERTY input.  The
//  engine asks for StorageDeviceProperty (manufacturer/model/serial) and
//  StorageAccessAlignmentProperty (physical sector size).  On Linux,
//  DeviceIoControl shims these to /sys/block lookups or returns
//  ERROR_INVALID_FUNCTION; engine falls back gracefully.
typedef enum _STORAGE_PROPERTY_ID {
    StorageDeviceProperty                   = 0,
    StorageAdapterProperty                  = 1,
    StorageDeviceIdProperty                 = 2,
    StorageDeviceUniqueIdProperty           = 3,
    StorageDeviceWriteCacheProperty         = 4,
    StorageMiniportProperty                 = 5,
    StorageAccessAlignmentProperty          = 6,
    StorageDeviceSeekPenaltyProperty        = 7,
    StorageDeviceTrimProperty               = 8,
    StorageDeviceWriteAggregationProperty   = 9,
    StorageDeviceDeviceTelemetryProperty    = 10,
    StorageDeviceLBProvisioningProperty     = 11,
    StorageDevicePowerProperty              = 12,
    StorageDeviceCopyOffloadProperty        = 13,
    StorageDeviceResiliencyProperty         = 14,
} STORAGE_PROPERTY_ID, *PSTORAGE_PROPERTY_ID;

typedef enum _STORAGE_QUERY_TYPE {
    PropertyStandardQuery                   = 0,
    PropertyExistsQuery                     = 1,
    PropertyMaskQuery                       = 2,
    PropertyQueryMaxDefined                 = 3,
} STORAGE_QUERY_TYPE, *PSTORAGE_QUERY_TYPE;

typedef struct _STORAGE_PROPERTY_QUERY {
    STORAGE_PROPERTY_ID PropertyId;
    STORAGE_QUERY_TYPE  QueryType;
    BYTE                AdditionalParameters[ 1 ];
} STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY;

typedef struct _STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    DWORD   BytesPerCacheLine;
    DWORD   BytesOffsetForCacheAlignment;
    DWORD   BytesPerLogicalSector;
    DWORD   BytesPerPhysicalSector;
    DWORD   BytesOffsetForSectorAlignment;
} STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR, *PSTORAGE_ACCESS_ALIGNMENT_DESCRIPTOR;

typedef struct _STORAGE_DEVICE_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    BYTE    DeviceType;
    BYTE    DeviceTypeModifier;
    BOOLEAN RemovableMedia;
    BOOLEAN CommandQueueing;
    DWORD   VendorIdOffset;
    DWORD   ProductIdOffset;
    DWORD   ProductRevisionOffset;
    DWORD   SerialNumberOffset;
    BYTE    BusType;
    DWORD   RawPropertiesLength;
    BYTE    RawDeviceProperties[ 1 ];
} STORAGE_DEVICE_DESCRIPTOR, *PSTORAGE_DEVICE_DESCRIPTOR;

typedef struct _STORAGE_DESCRIPTOR_HEADER {
    DWORD   Version;
    DWORD   Size;
} STORAGE_DESCRIPTOR_HEADER, *PSTORAGE_DESCRIPTOR_HEADER;

//  IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS output.  Engine reads the
//  DiskNumber of the first physical extent to map a logical volume to
//  the underlying disk it lives on.  On Linux we don't have a clean
//  equivalent — DeviceIoControl will return ERROR_INVALID_FUNCTION
//  and the engine treats the lookup as failed.
typedef struct _DISK_EXTENT {
    DWORD           DiskNumber;
    LARGE_INTEGER   StartingOffset;
    LARGE_INTEGER   ExtentLength;
} DISK_EXTENT, *PDISK_EXTENT;

typedef struct _VOLUME_DISK_EXTENTS {
    DWORD           NumberOfDiskExtents;
    DISK_EXTENT     Extents[ 1 ];
} VOLUME_DISK_EXTENTS, *PVOLUME_DISK_EXTENTS;

//  IOCTL_DISK_GET_DRIVE_LAYOUT_EX output — the engine reads PartitionStyle
//  and the partition info for the first partition only.  On Linux this
//  is also unsupported; partition layout queries go through libblkid in a
//  proper port, but the engine call sites tolerate failure.
typedef enum _PARTITION_STYLE {
    PARTITION_STYLE_MBR     = 0,
    PARTITION_STYLE_GPT     = 1,
    PARTITION_STYLE_RAW     = 2,
} PARTITION_STYLE;

typedef struct _PARTITION_INFORMATION_MBR {
    BYTE            PartitionType;
    BOOLEAN         BootIndicator;
    BOOLEAN         RecognizedPartition;
    DWORD           HiddenSectors;
} PARTITION_INFORMATION_MBR, *PPARTITION_INFORMATION_MBR;

typedef struct _PARTITION_INFORMATION_GPT {
    GUID            PartitionType;
    GUID            PartitionId;
    ULONGLONG       Attributes;
    WCHAR           Name[ 36 ];
} PARTITION_INFORMATION_GPT, *PPARTITION_INFORMATION_GPT;

typedef struct _PARTITION_INFORMATION_EX {
    PARTITION_STYLE PartitionStyle;
    LARGE_INTEGER   StartingOffset;
    LARGE_INTEGER   PartitionLength;
    DWORD           PartitionNumber;
    BOOLEAN         RewritePartition;
    BOOLEAN         IsServicePartition;
    union {
        PARTITION_INFORMATION_MBR Mbr;
        PARTITION_INFORMATION_GPT Gpt;
    };
} PARTITION_INFORMATION_EX, *PPARTITION_INFORMATION_EX;

typedef struct _DRIVE_LAYOUT_INFORMATION_MBR {
    DWORD           Signature;
    DWORD           CheckSum;
} DRIVE_LAYOUT_INFORMATION_MBR, *PDRIVE_LAYOUT_INFORMATION_MBR;

typedef struct _DRIVE_LAYOUT_INFORMATION_GPT {
    GUID            DiskId;
    LARGE_INTEGER   StartingUsableOffset;
    LARGE_INTEGER   UsableLength;
    DWORD           MaxPartitionCount;
} DRIVE_LAYOUT_INFORMATION_GPT, *PDRIVE_LAYOUT_INFORMATION_GPT;

typedef struct _DRIVE_LAYOUT_INFORMATION_EX {
    DWORD               PartitionStyle;
    DWORD               PartitionCount;
    union {
        DRIVE_LAYOUT_INFORMATION_MBR Mbr;
        DRIVE_LAYOUT_INFORMATION_GPT Gpt;
    };
    PARTITION_INFORMATION_EX        PartitionEntry[ 1 ];
} DRIVE_LAYOUT_INFORMATION_EX, *PDRIVE_LAYOUT_INFORMATION_EX;

typedef struct _STORAGE_WRITE_CACHE_PROPERTY {
    DWORD   Version;
    DWORD   Size;
    DWORD   WriteCacheType;
    DWORD   WriteCacheEnabled;
    DWORD   WriteCacheChangeable;
    DWORD   WriteThroughSupported;
    BOOLEAN FlushCacheSupported;
    BOOLEAN UserDefinedPowerProtection;
    BOOLEAN NVCacheEnabled;
} STORAGE_WRITE_CACHE_PROPERTY, *PSTORAGE_WRITE_CACHE_PROPERTY;

typedef struct _STORAGE_ADAPTER_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    DWORD   MaximumTransferLength;
    DWORD   MaximumPhysicalPages;
    DWORD   AlignmentMask;
    BOOLEAN AdapterUsesPio;
    BOOLEAN AdapterScansDown;
    BOOLEAN CommandQueueing;
    BOOLEAN AcceleratedTransfer;
    BYTE    BusType;
    WORD    BusMajorVersion;
    WORD    BusMinorVersion;
} STORAGE_ADAPTER_DESCRIPTOR, *PSTORAGE_ADAPTER_DESCRIPTOR;

typedef struct _DEVICE_SEEK_PENALTY_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    BOOLEAN IncursSeekPenalty;
} DEVICE_SEEK_PENALTY_DESCRIPTOR, *PDEVICE_SEEK_PENALTY_DESCRIPTOR;

typedef struct _DEVICE_TRIM_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    BOOLEAN TrimEnabled;
} DEVICE_TRIM_DESCRIPTOR, *PDEVICE_TRIM_DESCRIPTOR;

typedef struct _DEVICE_COPY_OFFLOAD_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    DWORD   MaximumTokenLifetime;
    DWORD   DefaultTokenLifetime;
    ULONGLONG MaximumTransferSize;
    ULONGLONG OptimalTransferCount;
    DWORD   MaximumDataDescriptors;
    DWORD   MaximumTransferLengthPerDescriptor;
    DWORD   OptimalTransferLengthPerDescriptor;
    WORD    OptimalTransferLengthGranularity;
    BYTE    Reserved[2];
} DEVICE_COPY_OFFLOAD_DESCRIPTOR, *PDEVICE_COPY_OFFLOAD_DESCRIPTOR;

//  FSCTL_MARK_HANDLE payload — Win32 uses this to steer reads to a particular
//  replica on storage-spaces volumes.  No Linux equivalent; the DeviceIoControl
//  shim recognizes the IOCTL code and returns success no-op.
typedef struct _MARK_HANDLE_INFO {
    union {
        DWORD       UsnSourceInfo;
        DWORD       CopyNumber;
    };
    HANDLE          VolumeHandle;
    DWORD           HandleInfo;
} MARK_HANDLE_INFO, *PMARK_HANDLE_INFO;

//  FSCTL_SET_SPARSE payload (optional under Win32 too — passing NULL
//  buffer also marks sparse).  Linux files are sparse-by-default at every
//  filesystem ESE is likely to land on (ext4/xfs/btrfs/zfs), so the shim
//  also no-ops this.
typedef struct _FILE_SET_SPARSE_BUFFER {
    BOOLEAN     SetSparse;
} FILE_SET_SPARSE_BUFFER, *PFILE_SET_SPARSE_BUFFER;

//  FSCTL_SET_ZERO_DATA payload — punches a hole in a sparse file.  Linux
//  equivalent: fallocate(FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE).
typedef struct _FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER   FileOffset;
    LARGE_INTEGER   BeyondFinalZero;
} FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;

//  FSCTL_QUERY_ALLOCATED_RANGES payload.  Input is one range
//  (FileOffset+Length), output is an array of allocated regions inside
//  it.  Linux equivalent: ioctl(FS_IOC_FIEMAP) or lseek(SEEK_HOLE/SEEK_DATA).
typedef struct _FILE_ALLOCATED_RANGE_BUFFER {
    LARGE_INTEGER   FileOffset;
    LARGE_INTEGER   Length;
} FILE_ALLOCATED_RANGE_BUFFER, *PFILE_ALLOCATED_RANGE_BUFFER;

//  FSCTL / IOCTL numeric codes the engine references.  Win32 builds these
//  via CTL_CODE; for the shim we just need stable distinct integers that
//  DeviceIoControl can dispatch on.
#ifndef FSCTL_MARK_HANDLE
#define FSCTL_MARK_HANDLE                       0x000900FCu
#define FSCTL_SET_SPARSE                        0x000900C4u
#define FSCTL_SET_ZERO_DATA                     0x000980C8u
#define FSCTL_QUERY_ALLOCATED_RANGES            0x000940CFu
#define IOCTL_STORAGE_QUERY_PROPERTY            0x002D1400u
#define IOCTL_DISK_GET_CACHE_INFORMATION        0x00074080u
#define IOCTL_DISK_PERFORMANCE                  0x00070020u
#define IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS    0x00560000u
#define IOCTL_DISK_GET_DRIVE_LAYOUT_EX          0x00074050u
#define FSCTL_SET_COMPRESSION                   0x0009C040u
#define FSCTL_GET_COMPRESSION                   0x0009003Cu
#endif

#ifndef COMPRESSION_FORMAT_NONE
#define COMPRESSION_FORMAT_NONE                 0x0000u
#define COMPRESSION_FORMAT_DEFAULT              0x0001u
#define COMPRESSION_FORMAT_LZNT1                0x0002u
#endif

//  MARK_HANDLE_INFO::HandleInfo bit flags.
#ifndef MARK_HANDLE_READ_COPY
#define MARK_HANDLE_READ_COPY           0x00000080u
#define MARK_HANDLE_NOT_READ_COPY       0x00000100u
#endif

typedef enum _STORAGE_BUS_TYPE {
    BusTypeUnknown = 0,
    BusTypeScsi    = 1,
    BusTypeAtapi   = 2,
    BusTypeAta     = 3,
    BusType1394    = 4,
    BusTypeSsa     = 5,
    BusTypeFibre   = 6,
    BusTypeUsb     = 7,
    BusTypeRAID    = 8,
    BusTypeiScsi   = 9,
    BusTypeSas     = 0xA,
    BusTypeSata    = 0xB,
    BusTypeSd      = 0xC,
    BusTypeMmc     = 0xD,
    BusTypeVirtual = 0xE,
    BusTypeFileBackedVirtual = 0xF,
    BusTypeNvme    = 0x11,
} STORAGE_BUS_TYPE;

#ifdef __cplusplus
}
#endif
