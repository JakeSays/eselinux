# windows-shim

A self-contained replacement for the Windows SDK headers the ESE engine
consumes.  When the engine's upstream sources `#include <windows.h>`,
`#include <winnt.h>`, `#include <strsafe.h>`, and so on, the Linux build
finds *these* headers first instead of the real Windows SDK.  That
keeps the upstream `.cxx` files compilable unchanged.

## The portability seam

The Linux port treats the Win32 API surface as its portability layer
— the same surface Windows itself uses.  Two pieces meet at this seam:

1. **`windows-shim/*.h`** (this directory): declarations of every Win32
   type, struct, enum, and function the engine reaches for.  Source
   compatibility only — these are headers, no code.
2. **`dev/ese/src/os/posix/winapi_*.cxx`**: Linux implementations of
   the functions declared here, written against POSIX primitives
   (`pthread`, `io_uring`, `epoll`, `mmap`, `pread`, `pwritev`, `flock`,
   `inotify`, `/proc`, `/sys/block`, etc.).

This is the rule for absorbing new Win32-only code: **add a decl to
the appropriate `windows-shim/*.h`, then back it with a real impl in
the matching `winapi_*.cxx`** — never with an `#ifdef _WIN32` gate
inside an upstream `.cxx`, and never with a parallel `_posix.cxx`
sibling that fork-and-rewrites a Windows file.  When a Win32 call has
no clean Linux equivalent (an internal-FSCTL DeviceIoControl, the
VSS / shadow surface, MARK_HANDLE replica steering, ...), the impl
can still ship as a stub that returns `ERROR_INVALID_FUNCTION` or
`ERROR_NOT_SUPPORTED` — the engine's existing `if ( !fSucceeded )`
paths already handle that.

## What's in the shim

| Header               | What it provides                                                                |
|----------------------|---------------------------------------------------------------------------------|
| `windows.h`          | Top-level umbrella; pulls in `winnt.h`, `winerror.h`, `tchar.h`, `intsafe.h`, ... |
| `winnt.h`            | Win32 base types (HANDLE, DWORD, LARGE_INTEGER, ULONG_PTR, ...), GUID, calling conventions, the structured-exception machinery |
| `winerror.h`         | `ERROR_*` numeric constants the engine references                                |
| `winioctl.h`         | `IOCTL_DISK_*`, `FSCTL_*`, `STORAGE_*`, `SMART_*`, `DISK_PERFORMANCE` shapes     |
| `winperf.h`          | Performance-counter ABI declarations                                             |
| `winapifamily.h`     | `WINAPI_FAMILY_PARTITION` macros — desktop everywhere                            |
| `wincrypt.h`         | CryptoAPI types — `encrypt_posix.cxx` is a libsodium target                       |
| `tchar.h`            | `_TCHAR` / `_T()` / the `_tcs*` family, gated on `_UNICODE`                       |
| `intsafe.h`          | The small `ULongAdd` / `ULongMult` subset the engine uses                        |
| `strsafe.h`          | `StringCb*` / `StringCch*` family — Linux impl lives in `winapi_strsafe.cxx`     |
| `intrin.h`           | MSVC intrinsics mapped to clang/gcc equivalents (`_rotl`, `__cpuid`, ...)        |
| `excpt.h`            | `__try` / `__except` / `GetExceptionInformation` — backed by SIGSEGV + `sigsetjmp` in `winapi_seh.cxx` |
| `ntstatus.h`         | `STATUS_*` codes                                                                 |
| `process.h`          | `_beginthreadex` / `_endthreadex` / `_getpid` shape                              |
| `conio.h`            | `_getch` / `_kbhit` / `_putch` / `_cprintf`                                     |
| `werapi.h`           | Windows Error Reporting — `WerRegister*` stubs                                  |
| `sddl.h`             | Security descriptor strings (stub — engine doesn't read SDs on Linux)           |
| `guiddef.h`          | `GUID` struct + `IsEqualGUID`                                                    |
| `ntverp.h`           | Build-versioning macros                                                          |
| `winapifamily.h`     | Partition family macros                                                          |
| `specstrings.h`      | Legacy SAL annotations (`__in`, `__out`, ...) — no-ops                           |
| `pshpack1/2/4/8.h`, `poppack.h` | `#pragma pack(push, N)` / `#pragma pack(pop)` aliases               |
| `minmax.h`           | Intentionally empty; `min`/`max` are heterogeneous overloads in `osstd_.hxx`     |
| `platform.h`         | Empty stub; the canonical `platform.h` (`ESE_OS_*`, `ESE_ARCH_*`, `ESE_COMPILER_*`) lives in `dev/ese/published/inc/platform.h` and is reached via `cc.hxx` |
| `rpc.h`              | Stub — the engine doesn't use the Windows RPC surface on Linux                  |

## How the build wires it up

The shim is prepended to the include path of every Linux build target
that ports upstream Windows code (the `osposix` static library, the
ESE shared library, the consumer binaries, etc.).  Tests that consume
the engine through the **public** JET API surface only — like the
`ese-tests` integration suite — do *not* include the shim; they use
the freestanding `jetapi.h` at the repository root, which depends only
on `<stdint.h>` (plus `<uchar.h>` in C for `char16_t`).

## Things to know when extending

- **Pack to 8 on 64-bit, 4 on 32-bit.**  Use `ESE_ARCH_64BIT` (from
  `dev/ese/published/inc/platform.h`) as the gate.  Don't key off
  `_WIN64` — that's never set on Linux.
- **`HANDLE` is `void*`.**  Linux-side `HANDLE` values are pointers to
  kernel-object descriptors (`KObject`) allocated by the shim's
  `AllocKObject` / `KToHandle` (`dev/ese/src/os/posix/winapi_kobject.hxx`).
  `INVALID_HANDLE_VALUE` is `((HANDLE)-1)`; callers that need to
  distinguish "null" from "invalid" must check both.
- **`SetLastError` / `GetLastError` are per-thread.**  Backed by
  `winapi_lasterror.cxx`; survives the same way Win32 does.
- **`WCHAR` is 16-bit.**  The engine builds with `-fshort-wchar`; the
  shim declares `WCHAR` as 16-bit and matches Windows' wide-character
  ABI.  The freestanding `jetapi.h` uses `char16_t` for the same role.
- **Retry POSIX syscalls on `EINTR`.**  Win32 callers have no concept
  of signal interruption; never surface `EINTR` as a read/write/lock
  failure.  All shim implementations already follow this rule — keep
  it that way when you add new ones.
