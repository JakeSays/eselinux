// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Minimal stubs for the os/osfile.cxx surface that osdisk.cxx (now
// compiled on Linux for the oslayer_iomgr_test runner) calls into.
// The full os/osfile.cxx is ~3200 lines and pulls in the Win32 mem-
// mapped-file, FSCTL_*, MARK_HANDLE_INFO, FILE_RENAME_INFO and other
// surfaces that have no portable Linux equivalent without significant
// porting work; the test runner only needs the six symbols below.

#include "osstd.hxx"

//  IOREQ::cRetriesMax - out-of-line definition for the in-class static
//  const declared in os/_osdisk.hxx. MSVC tolerates the in-class
//  initializer alone for odr-use; clang requires a translation-unit-
//  level definition. (C++17 implicit-inline applies to static
//  *constexpr*; the upstream class uses static *const*.)
const INT IOREQ::cRetriesMax;

//  ErrOSFileIFromWinError_ - maps Win32 GLE values to JET errors. The
//  Linux engine path also produces Win32-style error codes via the
//  windows-shim's SetLastError, so the same translation table applies
//  verbatim. Lift the body from osfile.cxx (this file is for shim use
//  only, so the symbol lives next to the rest of the OS-layer impl).
ERR ErrOSFileIFromWinError_( _In_ const DWORD error, _In_z_ PCSTR szFile, _In_ const LONG lLine )
{
    switch ( error )
    {
        case ERROR_SUCCESS:
            return JET_errSuccess;
        case ERROR_IO_PENDING:
            return wrnIOPending;
        case ERROR_INVALID_PARAMETER:
        case ERROR_CALL_NOT_IMPLEMENTED:
        case ERROR_INVALID_ADDRESS:
        case ERROR_NOT_SUPPORTED:
            return ErrERRCheck_( JET_errInvalidParameter, szFile, lLine );
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return ErrERRCheck_( JET_errFileNotFound, szFile, lLine );
        case ERROR_TOO_MANY_OPEN_FILES:
        case ERROR_NO_MORE_FILES:
            return ErrERRCheck_( JET_errOutOfFileHandles, szFile, lLine );
        case ERROR_INVALID_HANDLE:
            return ErrERRCheck_( JET_errFileInvalidType, szFile, lLine );
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return ErrERRCheck_( JET_errOutOfMemory, szFile, lLine );
        case ERROR_HANDLE_DISK_FULL:
        case ERROR_DISK_FULL:
            return ErrERRCheck_( JET_errDiskFull, szFile, lLine );
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return ErrERRCheck_( JET_errFileAccessDenied, szFile, lLine );
        case ERROR_DISK_CORRUPT:
            return ErrERRCheck_( JET_errDiskIO, szFile, lLine );
        case ERROR_OPERATION_ABORTED:
            return ErrERRCheck_( JET_errFileIOAbort, szFile, lLine );
        case ERROR_NO_SYSTEM_RESOURCES:
            return ErrERRCheck_( JET_errOutOfMemory, szFile, lLine );
    }
    //  Unrecognised: surface as generic disk-IO failure.
    return ErrERRCheck_( JET_errDiskIO, szFile, lLine );
}

//  FOSFileISyncIOREQ / FOSFileIZeroingSyncIOREQ / FOSFileIExtendingSyncIOREQ
//  - on Windows these check whether pioreq->pfnCompletion is one of the three
//  Win32 sync-IO completion callbacks (IOSyncComplete_, IOZeroingWriteComplete_,
//  IOExtendingWriteComplete_) that live in osfile.cxx. On Linux those callbacks
//  are never installed: the OVERLAPPED/ErrorIOMgrIssueIO path in osdisk.cxx is
//  gated to a stub returning ERROR_INVALID_FUNCTION. Returning FALSE here is
//  correct — no live IOREQ on Linux will carry those completion pointers.
BOOL COSFile::FOSFileISyncIOREQ( const IOREQ * const /*pioreq*/ )
{
    return FALSE;
}

BOOL COSFile::FOSFileIZeroingSyncIOREQ( const IOREQ * const /*pioreq*/ )
{
    return FALSE;
}

BOOL COSFile::FOSFileIExtendingSyncIOREQ( const IOREQ * const /*pioreq*/ )
{
    return FALSE;
}

//  COSFile::FOSFileSyncComplete - decides whether an IOREQ that just
//  finished is one of the synchronous variants the engine treats as
//  "issue inline". Pure pioreq-field logic, portable.
BOOL COSFile::FOSFileSyncComplete( const IOREQ * const pioreq )
{
    return ( qosIODispatchImmediate == ( pioreq->grbitQOS & qosIODispatchMask ) ) &&
           ( FOSFileISyncIOREQ( pioreq ) ||
             FOSFileIZeroingSyncIOREQ( pioreq ) ||
             FOSFileIExtendingSyncIOREQ( pioreq ) );
}

//  COSFile IO completion callbacks — these are installed by the Win32 IO-issue
//  path in osfile.cxx (ErrIOAsync, ErrIOWrite extending-write machinery).
//  That path is not yet ported to Linux; COSFile itself lands in Phase 7.
//  Providing no-op stubs satisfies the linker references from osdisk.cxx's
//  DEBUG-mode Assert() checks (which take the address of these functions via
//  PFN() casts). They will never be called on Linux.
void COSFile::IOSyncComplete_(
    const ERR               /*err*/,
    COSFile* const          /*posf*/,
    const FullTraceContext& /*tc*/,
    OSFILEQOS               /*grbitQOS*/,
    const QWORD             /*ibOffset*/,
    const DWORD             /*cbData*/,
    BYTE* const             /*pbData*/,
    CIOComplete* const      /*piocomplete*/ )
{
}

void COSFile::IOZeroingWriteComplete_(
    const ERR                       /*err*/,
    COSFile* const                  /*posf*/,
    const FullTraceContext&         /*tc*/,
    const OSFILEQOS                 /*grbitQOS*/,
    const QWORD                     /*ibOffset*/,
    const DWORD                     /*cbData*/,
    BYTE* const                     /*pbData*/,
    CExtendingWriteRequest* const   /*pewreq*/ )
{
}

void COSFile::IOExtendingWriteComplete_(
    const ERR                       /*err*/,
    COSFile* const                  /*posf*/,
    const FullTraceContext&         /*tc*/,
    const OSFILEQOS                 /*grbitQOS*/,
    const QWORD                     /*ibOffset*/,
    const DWORD                     /*cbData*/,
    BYTE* const                     /*pbData*/,
    CExtendingWriteRequest* const   /*pewreq*/ )
{
}

void COSFile::IOChangeFileSizeComplete_( CExtendingWriteRequest* const /*pewreq*/ )
{
}

//  _OSFILE::Pfsconfig - trivial getter the IO scheduler (osdisk.cxx)
//  uses to route per-file event reporting.
IFileSystemConfiguration* const _OSFILE::Pfsconfig() const
{
    return pfsconfig;
}

//  _OSFILE::ErrSetReadCopyNumber - on Windows this issues an
//  FSCTL_MARK_HANDLE with MARK_HANDLE_READ_COPY to steer reads to a
//  particular replica. Linux has no equivalent (file replication is
//  filesystem-private); succeed unconditionally so callers don't get
//  blocked.
ERR _OSFILE::ErrSetReadCopyNumber( LONG /*iCopyNumber*/ )
{
    return JET_errSuccess;
}

//  SetDiskMappingMode / GetDiskMappingMode - the disk-mapping
//  getter/setter normally live in os/osfs.cxx. The osposix build
//  doesn't carry that file (osfs_posix replaces it). The forward
//  declarations in os/_osfs.hxx use the engine's INLINE macro
//  (= `inline`); we provide non-inline (`__attribute__((used))`)
//  definitions here so the linker has a strong body to bind against
//  even when the inline declaration would otherwise let the body be
//  discarded under -O0.
static OSDiskMappingMode g_diskMappingMode = eOSDiskInvalidMode;
__attribute__((used))
void SetDiskMappingMode( const OSDiskMappingMode diskMode )
{
    g_diskMappingMode = diskMode;
}
__attribute__((used))
OSDiskMappingMode GetDiskMappingMode()
{
    return g_diskMappingMode;
}
