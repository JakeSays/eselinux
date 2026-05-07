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
