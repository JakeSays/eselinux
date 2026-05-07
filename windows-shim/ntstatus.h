// Linux shim for <ntstatus.h>. This Windows kernel header defines
// STATUS_xxx codes (NTSTATUS values). The devlibtest code path that
// references it (MemoryMappedIoSuite for STATUS_CRC_ERROR) compiles
// fine on Linux without the symbol — the comment in osunittest.hxx is
// stale. Stub to a tiny set of NTSTATUS constants in case any test
// needs them later.
#pragma once

#ifndef STATUS_CRC_ERROR
#define STATUS_CRC_ERROR        ((LONG)0xC000003FL)
#endif
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS          ((LONG)0x00000000L)
#endif
