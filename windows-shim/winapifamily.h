// Linux shim for <winapifamily.h>. WinRT/UWP partition macros — the
// Linux port targets desktop only, so all WINAPI_PARTITION_* checks
// resolve to "in partition".
#pragma once

#define WINAPI_PARTITION_DESKTOP        0x00000001
#define WINAPI_PARTITION_APP            0x00000002
#define WINAPI_PARTITION_PC_APP         0x00000004
#define WINAPI_PARTITION_PHONE_APP      0x00000008
#define WINAPI_PARTITION_SYSTEM         0x00000010
#define WINAPI_PARTITION_SERVER         0x00000020
#define WINAPI_PARTITION_GAMES          0x00000040
// PKG_ESENT is the partition for ESE-as-an-OS-component (vs. shipped as a
// standalone library). The engine's public header gates several W-variant
// declarations behind it; aliasing it to DESKTOP keeps the Linux build with
// the full API surface.
#define WINAPI_PARTITION_PKG_ESENT      WINAPI_PARTITION_DESKTOP

#define WINAPI_FAMILY                   WINAPI_PARTITION_DESKTOP
#define WINAPI_FAMILY_PARTITION(p)      ((WINAPI_FAMILY & (p)) != 0)

#define WINAPI_FAMILY_DESKTOP_APP       WINAPI_PARTITION_DESKTOP
#define WINAPI_FAMILY_APP               WINAPI_PARTITION_DESKTOP
#define WINAPI_FAMILY_PC_APP            WINAPI_PARTITION_DESKTOP
