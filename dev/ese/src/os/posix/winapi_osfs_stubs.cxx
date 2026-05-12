// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Stubs for upstream osfs.cxx forward-declared APIs that live behind
// Win10+ / Win8+ feature loaders.  Each function is declared locally
// inside osfs.cxx without extern "C", so the engine's
// NTOSFuncStd / FunctionLoaderStaticShim takes the C++-mangled symbol.
// We mirror the same local type declarations here so the stubs we emit
// have matching mangled names.
//
// Engine call sites all guard on ErrIsPresent() / NT_SUCCESS() and fall
// back to the Win5x / non-packaged path on failure — which is exactly
// what these stubs report.

#include "osstd.hxx"
#include "winapi_kobject.hxx"
#include <ntstatus.h>

//  windows-shim/specstrings.h #defines __reserved → empty as a SAL annotation.
//  <linux/fs.h> transitively pulls fscrypt.h, which has a struct field
//  literally named __reserved.  Drop the macro just before the kernel headers.
#undef __reserved

#include <errno.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

using osposix::HandleKind;
using osposix::HandleToK;
using osposix::KObject;

//  Mirror osfs.cxx's NtQueryVolumeInformationFile forward declaration.

typedef LONG NTSTATUS;

typedef enum _FSINFOCLASS {
    FileFsVolumeInformation       = 1,
    FileFsLabelInformation,
    FileFsSizeInformation,
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsDriverPathInformation,
    FileFsVolumeFlagsInformation,
    FileFsSectorSizeInformation,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    } DUMMYUNIONNAME;
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

//  Engine queries FileFsSectorSizeInformation to drive sector-aligned IO
//  sizing.  Linux exposes the same data via ioctl(BLKSSZGET/BLKPBSZGET)
//  on the underlying block device; for regular files we fall back to
//  st_blksize.

typedef struct _FILE_FS_SECTOR_SIZE_INFORMATION {
    ULONG LogicalBytesPerSector;
    ULONG PhysicalBytesPerSectorForAtomicity;
    ULONG PhysicalBytesPerSectorForPerformance;
    ULONG FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
    ULONG Flags;
    ULONG ByteOffsetForSectorAlignment;
    ULONG ByteOffsetForPartitionAlignment;
} FILE_FS_SECTOR_SIZE_INFORMATION, *PFILE_FS_SECTOR_SIZE_INFORMATION;

NTSTATUS NtQueryVolumeInformationFile(HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID FsInformation,
    ULONG Length,
    FS_INFORMATION_CLASS FsInformationClass)
{
    if (IoStatusBlock)
    {
        IoStatusBlock->DUMMYUNIONNAME.Status = 0;
        IoStatusBlock->Information = 0;
    }
    KObject* const k = HandleToK(FileHandle);
    if (!k || k->kind != HandleKind::File || k->fileFd < 0 || !FsInformation)
        return STATUS_INVALID_HANDLE;

    if (FsInformationClass == FileFsSectorSizeInformation)
    {
        if (Length < sizeof(FILE_FS_SECTOR_SIZE_INFORMATION))
            return STATUS_BUFFER_TOO_SMALL;

        struct stat st;
        if (fstat(k->fileFd, &st) < 0)
            return STATUS_ACCESS_DENIED;

        //  ESE requires sector sizes to be powers of 2, >= 512, and <= 4096.
        //  The upper bound matches what ESE actually services on Windows:
        //  it fires FirewallTag:SectorSizeTooBig and clamps to 4K for
        //  anything larger (osfs.cxx around line 2021), and the log
        //  layer warns + clamps separately (logstream.cxx around line
        //  196).  The FireWall is benign — it's an audit trace, not a
        //  recovery action — but it spams the event log on every file
        //  open under filesystems like ZFS whose recordsize defaults to
        //  128 KB.  Cap the shim's output at 4K so the engine never
        //  sees the oversized value in the first place.
        //
        //  ioctl for block devices reports honest hardware values;
        //  st_blksize for regular files / directories on networked or
        //  automount filesystems can return non-power-of-2 numbers
        //  (2560, 3584, etc. — the kernel's preferred-IO hint).  Clamp
        //  those to 4096 as well.
        static constexpr unsigned int MaximumSectorSize = 4096;
        auto IsPow2 = [](unsigned int v) -> bool
        {
            return v != 0 && (v & (v - 1)) == 0;
        };
        auto ClampSectorSize = [&IsPow2](unsigned int candidate) -> unsigned int
        {
            if (candidate < 512 || !IsPow2(candidate))
            {
                return MaximumSectorSize;
            }
            return candidate > MaximumSectorSize ? MaximumSectorSize : candidate;
        };
        unsigned int logical = MaximumSectorSize;
        unsigned int physical = MaximumSectorSize;
        if (S_ISBLK(st.st_mode))
        {
            int logicalT = 0;
            int physicalT = 0;
            if (ioctl(k->fileFd, BLKSSZGET, &logicalT) == 0)
                logical = ClampSectorSize((unsigned int) logicalT);
            if (ioctl(k->fileFd, BLKPBSZGET, &physicalT) == 0)
                physical = ClampSectorSize((unsigned int) physicalT);
        }
        else
        {
            //  Regular file / directory: honour st_blksize when sane,
            //  otherwise fall back to the 4096 default.
            physical = ClampSectorSize((unsigned int) st.st_blksize);
            logical  = physical;
        }

        FILE_FS_SECTOR_SIZE_INFORMATION* const pInfo =
            (FILE_FS_SECTOR_SIZE_INFORMATION*) FsInformation;
        memset(pInfo, 0, sizeof(*pInfo));
        pInfo->LogicalBytesPerSector = logical;
        pInfo->PhysicalBytesPerSectorForAtomicity = physical;
        pInfo->PhysicalBytesPerSectorForPerformance = physical;
        pInfo->FileSystemEffectivePhysicalBytesPerSectorForAtomicity = physical;
        pInfo->ByteOffsetForSectorAlignment = 0;
        pInfo->ByteOffsetForPartitionAlignment = 0;
        if (IoStatusBlock)
            IoStatusBlock->Information = sizeof(*pInfo);
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_IMPLEMENTED;
}

//  Win8+ AppModel state API.  Linux is never "packaged"; engine takes
//  the non-packaged path so these don't actually get called, but the
//  linker still resolves their addresses through the FunctionLoader
//  template.

typedef void* HSTATE;

typedef enum tag_STATE_PERSIST_ATTRIB {
    STATE_PERSIST_UNDEFINED = 0,
    STATE_PERSIST_LOCAL,
    STATE_PERSIST_ROAMING,
    STATE_PERSIST_TEMP,
    STATE_PERSIST_LAST
} STATE_PERSIST_ATTRIB;

HSTATE OpenState()
{
    return nullptr;
}

BOOL CloseState(HSTATE /*hState*/)
{
    return TRUE;
}

BOOL GetStateFolder(HSTATE /*hState*/, STATE_PERSIST_ATTRIB /*persistAttrib*/,
    LPWSTR pPath, UINT32* pPathCch)
{
    if (pPathCch)
        *pPathCch = 0;
    if (pPath && pPathCch && *pPathCch > 0)
        pPath[0] = L'\0';
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

//  Engine packaged-process probe.  Linux never claims packaged status;
//  the variable g_fProcessIsPackaged lives in osfs.cxx and is initialised
//  to fFalse, so this just returns without touching it.
VOID CalculateCurrentProcessIsPackaged()
{
}

//  SetDiskMappingMode / GetDiskMappingMode used to live here as a strong
//  override because the upstream INLINE definitions in osfs.cxx didn't
//  always emit out-of-line bodies (cross-TU callers in osdisk.cxx then
//  failed to link at -O0).  That override caused a Release-mode bug:
//  -O3 inlined the upstream INLINE bodies into in-TU osfs.cxx callers,
//  so they used osfs.cxx's g_diskMode, while cross-TU callers from
//  osdisk.cxx hit this override and wrote a separate variable — leaving
//  ErrOSVolumeConnect's switch with eOSDiskInvalidMode and surfacing as
//  JET_errInternalError at log-file creation.  Fix: dropped INLINE from
//  osfs.cxx so the body is always emitted out-of-line; removed this
//  override.
