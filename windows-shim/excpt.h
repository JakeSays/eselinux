// Linux shim for <excpt.h>. SEH (__try/__except, GetExceptionInformation,
// EXCEPTION_POINTERS) is Windows-only; the Linux port replaces SEH-using
// code with conventional error returns. This stub provides just enough
// for error.hxx to parse.
#pragma once

#include <stdint.h>

typedef struct _EXCEPTION_POINTERS EXCEPTION_POINTERS, *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;

// SEH "filter" return values, kept for source compatibility.
#define EXCEPTION_EXECUTE_HANDLER       1
#define EXCEPTION_CONTINUE_SEARCH       0
#define EXCEPTION_CONTINUE_EXECUTION   (-1)

// Common NTSTATUS-shaped exception codes used by the engine.
#define STATUS_ACCESS_VIOLATION         ((unsigned long)0xC0000005)
#define STATUS_IN_PAGE_ERROR            ((unsigned long)0xC0000006)
#define STATUS_DISK_FULL                ((unsigned long)0xC000007F)

#ifdef __cplusplus
extern "C" {
#endif

// SEH intrinsics — compile-time stubs. Any code that actually executes a
// __try/__except block must be ported away from SEH on Linux; these stubs
// only let headers parse.
inline EXCEPTION_POINTERS* GetExceptionInformation(void) { return nullptr; }
inline unsigned long GetExceptionCode(void) { return 0; }

#ifdef __cplusplus
}
#endif

// SEH keyword no-ops for Linux. clang on linux x86_64 does not implement
// __try/__except. The engine guards meaningful uses with ENABLE_EXCEPTIONS,
// but several .cxx files use raw __try/__except keywords directly. Map
// them to a control-flow shape that compiles and behaves as if the filter
// always returned EXCEPTION_CONTINUE_SEARCH (i.e. handler never runs):
//
//   __try { body } __except( filter ) { handler }
// becomes
//   { body } if (0) { handler }
//
// The filter expression is dropped — fine, since it's never evaluated.
// __finally { cleanup } becomes a plain compound statement, so cleanup
// runs unconditionally on the success path (its only path on Linux).
#ifndef _MSC_VER
#define __try
#define __except(filter) if (0)
#define __finally
#define __leave do { } while (0)
#endif
