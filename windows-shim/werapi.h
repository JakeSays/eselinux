// Linux shim for <werapi.h> (Windows Error Reporting). The engine only
// uses WerRegisterMemoryBlock to attach context buffers to crash dumps.
// On Linux there is no equivalent system service, so the posix layer
// implements this as a no-op (returns S_OK) — the data simply doesn't
// flow into an external diagnostics pipeline until LTTng / coredump
// integration lands.
#pragma once

#include <winnt.h>
#include <winerror.h>

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WerRegisterMemoryBlock( PVOID pvAddress, DWORD dwSize );
HRESULT WerUnregisterMemoryBlock( PVOID pvAddress );

#ifdef __cplusplus
} // extern "C"
#endif
