// Linux shim for <ntstatus.h>. This Windows kernel header defines
// STATUS_xxx codes (NTSTATUS values). The devlibtest code path that
// references it (MemoryMappedIoSuite for STATUS_CRC_ERROR) compiles
// fine on Linux without the symbol — the comment in osunittest.hxx is
// stale. Stub to a tiny set of NTSTATUS constants in case any test
// needs them later.
#pragma once

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                  ((LONG) 0x00000000L)
#define STATUS_UNSUCCESSFUL             ((LONG) 0xC0000001L)
#define STATUS_NOT_IMPLEMENTED          ((LONG) 0xC0000002L)
#define STATUS_INVALID_INFO_CLASS       ((LONG) 0xC0000003L)
#define STATUS_INFO_LENGTH_MISMATCH     ((LONG) 0xC0000004L)
#define STATUS_ACCESS_VIOLATION         ((LONG) 0xC0000005L)
#define STATUS_INVALID_HANDLE           ((LONG) 0xC0000008L)
#define STATUS_INVALID_PARAMETER        ((LONG) 0xC000000DL)
#define STATUS_END_OF_FILE              ((LONG) 0xC0000011L)
#define STATUS_ACCESS_DENIED            ((LONG) 0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL         ((LONG) 0xC0000023L)
#define STATUS_DISK_FULL                ((LONG) 0xC000007FL)
#define STATUS_CRC_ERROR                ((LONG) 0xC000003FL)
#endif
