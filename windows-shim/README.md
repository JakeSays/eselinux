# windows-shim

Minimal Linux-side replacements for Windows SDK headers consumed by the ESE
codebase. These headers are added to the include path of the `osposix` target
(and any other Linux build target that needs them) ahead of the system include
paths.

## Status

Phase 1 of the Linux port — these headers exist as empty `#pragma once` stubs
to make `cmake -B build` configure cleanly on Linux. Real type definitions
land in Phase 3 (foundational headers) as compile errors surface in actual
source files.

## Files

| File           | Replaces                                           |
|----------------|----------------------------------------------------|
| `windows.h`    | `windows.h` master header                          |
| `winnt.h`      | NT typedefs (HANDLE, DWORD, LARGE_INTEGER, …)      |
| `tchar.h`      | TCHAR macros                                       |
| `minmax.h`     | `min`/`max` macros                                 |
| `specstrings.h`| SAL annotations (no-ops)                           |
| `pshpack1.h`   | `#pragma pack(push,1)`                             |
| `pshpack2.h`   | `#pragma pack(push,2)`                             |
| `pshpack4.h`   | `#pragma pack(push,4)`                             |
| `pshpack8.h`   | `#pragma pack(push,8)`                             |
| `poppack.h`    | `#pragma pack(pop)`                                |
| `winerror.h`   | `ERROR_*` constants used by osfile.cxx & friends   |
| `winioctl.h`   | `IOCTL_DISK_*`, `FSCTL_*`                          |
| `wincrypt.h`   | CryptoAPI types (encrypt.cxx will be rewritten)    |
| `intrin.h`     | MSVC intrinsic shims                               |
| `sddl.h`       | Security descriptors (stub)                        |
| `rpc.h`        | RPC (stub — engine doesn't use the RPC surface)    |
