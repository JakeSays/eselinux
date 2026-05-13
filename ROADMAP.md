# ESE Linux port — roadmap

Last refreshed: 2026-05-13 (commit `151cb0e`).

The aim of the port is a working Linux ESE: `libese.so` plus consuming applications that can create, open, write, read, and close JET databases through the same JET API surface Windows ESE exports.  Reading Windows-produced `.edb`/`.log` files is **not** in scope for the initial release (see `~/.claude/projects/-p-ese-repo/memory/project_port_scope.md`); the on-disk format is allowed to diverge between platforms.

## Where the port stands today

Build & link surface — solid.
- Clang 22 toolchain at `/apps/clang-22.1.3`, libc++/libc++abi/libunwind statically linked, `-std=c++20 -fshort-wchar -fvisibility=hidden`.
- `libese.so` + an optional `libese_tracepoints.so` (LTTng provider) and 13 consumer binaries (`BookStoreSample`, `CcLayerUnit`, `COLLECTIONUnit`, `ERRVALIDATOR`, `EseLibWithTestsRunner`, `ese-tests`, `ese-config-test`, `eseutil`, `IterQueryUnit`, `nls_smoke`, `RESMGRUNIT`, `STATUNIT`, `SYNCUNIT`) build clean in both Debug and Release.  Zero compiler warnings.
- Third-party deps wired: io_uring, libnls (vendored submodule under `third_party/libnls/`), libucl (vendored under `third_party/libucl-0.9.4/`), zstd 1.5.7 (vendored under `third_party/zstd-1.5.7/`).  Optional, dlopen-loaded at runtime: lttng-ust (used by `libese_tracepoints.so`), libsystemd (for `sd_journal_sendv` structured fields).  Neither is a hard build-time dep.
- Build modes: Debug at `build/`, Release at `build-release/`.  Both pass the suite.

Test surface — extensive and green.
- `EseLibWithTestsRunner` tier-1 (no `-d`, runs JETUNITTEST): default-on suite passes cleanly with 35 opt-in tests skipped.  Release matches Debug.
- `EseLibWithTestsRunner` tier-2 (`-d <dir>`, runs JETUNITTESTDB against a real JET database): 32 tests pass; 776 opt-in tests skipped.
- `devlibtest` (7 binaries: `CcLayerUnit`, `COLLECTIONUnit`, `ERRVALIDATOR`, `IterQueryUnit`, `RESMGRUNIT`, `STATUNIT`, `SYNCUNIT`): tens of millions of assertions across the OS, sync, resmgr, and collection layers.  All passing.  `RESMGRUNIT` alone runs 2.1M tests; `SYNCUNIT` 2.5M.
- `ese-tests` (public-JET-API integration scenarios at `integration-tests/`): 157 scenarios across DDL, DML, navigation, transactions, long values, multi-values, escrow, temporary tables, sessions, backup/restore, recovery (including SIGKILL crash), snapshots, maintenance (compact), schema, limits, errors, scale, and concurrency.  Coverage tracked in `integration-tests/COVERAGE.md` (42% of base JET surface).  Passes in Debug (~30s) and Release (~25s).
- `ese-config-test`: dedicated integration test for the libucl `/etc/ese.conf` + `<exe>.ese.conf` loader, via fork+exec so each subtest gets a pristine engine instance.
- All 7 originally-Windows-only test files (`daehelpers_test`, `osu_test`, `oslayer_test`, `logprereader_test`, `fmp_test`, `node_test`, `rbscleaner_test`) compile and pass on Linux.
- `run-all-tests.sh` + `run-all-tests` CMake target drive every test binary in one shot.  12/12 suites green on both Debug and Release.

Major plumbing pieces in place.
- `windows-shim/` (Win32 API surface) covers everything `osposix` + the engine reach for, including a real `__try`/`__except` via SIGSEGV + `sigsetjmp` (see `windows-shim/excpt.h` + `dev/ese/src/os/posix/winapi_seh.cxx`).
- Win32 NLS surface served by `libnls` (LGPL, vendored).  Replaces ICU.  Byte-compatible with Win32 (Wine-derived implementation against the same `.nls` data tables), keeping the door open for future on-disk format compat.
- Config layer (`config_posix.cxx`) reads `/etc/ese.conf` + `<exe>.ese.conf` via libucl — the registry-equivalent on Linux.
- `FormatMessageW` works against an mc-generated message table (`tools/mc/` C# native-AOT generator produces `jetmsgex_table.cxx`).
- File I/O: upstream `osfile.cxx` + `osfs.cxx` + `osdisk.cxx` now drive the live path.  The earlier parallel posix shims (`osdisk_posix.cxx`, `syncfile_posix.cxx`, the posix-specific `osfile`/`osfs` rewrites) have been retired.  `CIoUringFile` lives under the upstream `IFileAPI`, and IOREQ pool submissions route through io_uring (or the fall-back ReadFile/WriteFile path on filesystems without async support).
- Block-device identity wired against `/sys/block/<dev>/queue/rotational` + storage IOCTLs through `winapi_blockdev.cxx` and `winapi_blockdev_ioctl.cxx`, with a fallback for filesystems whose backing device isn't visible under `/sys`.
- Compression: Rtl* family backed by zstd 1.5.7 via `winapi_compression.cxx`.  Smoke test covers the journal compression round trip.
- Consumer entry points: `JetPlatformInitialize` / `JetPlatformTerminate` were added so `libese.so` consumers (eseutil, BookStoreSample, third-party callers) get the OS layer pre-init done for them.  `JetPlatformInitializeWithConfig(const char* path)` lets a host process point at an explicit config file (still merged on top of `/etc/ese.conf`).
- Standalone freestanding `jetapi.h` at repo root: customer-facing JET surface with no Windows-isms (stdint types throughout, `char16_t` for wide strings, JET_VERSION pinned at 0x0A01).  `integration-tests/` consumes only this header — no `windows-shim/`, no `dev/ese/published/inc/`.
- Event logging — admin events go to syslog(3) (baseline; captured by journald or any syslogd) with `sd_journal_sendv` as an opportunistic upgrade via libsystemd dlopen (no hard build dep).  Analytic events flow through a dlopen-loaded `libese_tracepoints.so` (LTTng UST), generated from `EseEtwEventsPregen.txt` by the `tools/etwlttng/` C# AOT tool.  Boxes without lttng-ust skip the .so and tracing stays quiet; lttng-tools captures all 77 event types when present.  See `~/.claude/projects/-p-ese-repo/memory/project_port_event_logging.md`.
- IO submit pre/post hooks mirror Win32 on the Linux arm of `ErrorIOMgrIssueIO`: `RFSAlloc` resource-failure gate, the three `ErrFaultInjection` IDs (17384 / 64738 / 42980), and `UtilThreadBeginLowIOPriority` / `EndLowIOPriority` bracketing.  The latter pair is wired through `SetThreadPriority`'s `THREAD_MODE_BACKGROUND_BEGIN/END` recognition in `winapi_thread.cxx`, which calls `ioprio_set(2)` on the current task (drops to `IOPRIO_CLASS_IDLE` on begin, restores to `IOPRIO_CLASS_NONE` on end).
- Encryption-at-rest: AES-256-GCM via libsodium (`crypto_aead_aes256gcm_*`), runtime-optional via dlopen.  `libese.so` has no link-time dependency on libsodium; on first use of any encryption entry point we `dlopen("libsodium.so.23")` and resolve five function pointers (`sodium_init`, `crypto_aead_aes256gcm_is_available`, `_encrypt`, `_decrypt`, `randombytes_buf`).  If the library isn't installed, or AES-NI / ARM CryptoExtension is unavailable, `ErrOSCreateAes256Key` / `ErrOSEncryptWithAes256` / `ErrOSDecryptWithAes256` return `JET_errFeatureNotAvailable` — engines that don't enable at-rest encryption are unaffected.  On-disk layout per encrypted blob: `[ciphertext (N bytes)][auth tag (16)][trailer Version=1, IV=16]`; only the first 12 bytes of the trailer's InitVector are used (the GCM nonce).  GCM's auth tag replaces the upstream CRC32-of-plaintext integrity check.

Known gaps that block "real users."
- **Some `winapi_*.cxx` functions remain unexercised** — surface that compiles and links but no test or live code path touches yet.  Not a gating problem; flagged for the eventual API-coverage audit (priority #1 below).
- **Templated `FOSEventTraceEnabled<etguid>()` still returns `fFalse`.**  A handful of engine call sites consult it to skip expensive data-gathering before an `ET*` call.  Wiring it to query lttng-ust's per-tracepoint enable state (`lttng_ust_tracepoint_ese___X.state`) is follow-up work; impact is minor since most ET* paths don't gate on the templated check.

## Priority list

### 1. JET public API coverage ratcheting

`ese-tests` is at 157 scenarios — 42% of base JET surface per `integration-tests/COVERAGE.md`.  Standing pull:

- Pick the next batch from COVERAGE.md's "not exercised" column.
- Land new scenarios; refresh COVERAGE.md.
- Promote any scenario that uncovers a real engine bug to a permanent regression (the Phase 6 Compact/OverwriteLV/Restore fixes were exactly this pattern).

This is also where to fold in the "untouched winapi_*.cxx functions" audit — anything the engine never calls is dead surface we can either delete or stub down.

### 2. Capture a Release perf baseline

Debug perf numbers are pinned at commit `31a90b1`; Release deltas
haven't been written down.  Run the perf-flagged tests
(`CPAGE.ReplacePerf`, `CHECKSUM.Perf`, the iouring micro-bench)
against a Release build and check the numbers into the repo so
future regressions are visible.

## Things explicitly out of scope (still)

- **Windows `.edb` / `.log` format compatibility.**  Deferred indefinitely.  libnls keeps the door open by producing Windows-byte-compatible sort keys, but the disk format on Linux can otherwise diverge freely (struct packing, ICU vs NLS bytes were the original concerns; both are addressed in a way that allows divergence).
- **VSS-equivalent backup integration.**  `noncore/eseshadow` is dropped from the Linux build.  Linux backups happen at the filesystem layer (LVM/Btrfs/ZFS snapshots), not via app-coordinated quiescence.
- **WinDbg extension `edbg.cxx`.**  21k LOC of `dbgeng.h` COM walking; pure dev-triage tool.  A GDB/LLDB Python equivalent is a future separate project, not a port deliverable.
- **GCC support.**  Clang only.  `-fms-extensions` + `-fdeclspec` + `-fdelayed-template-parsing` is Clang-only.

## What's in `bugs/`

- `bugs/cpage-ctagreserved-missing-mask.md` — upstream bug found while bringing up the `node_test.cxx` fuzz suites.  `PGHDR::itagState`'s "high bit reserved for future use" wasn't actually being masked at read sites; fixed in `c00f86b` + `5ccc4b1`.  Open question for Microsoft when filed upstream.

Three Phase-4-deferred TODOs were resolved in Phase 6 (`04bd25e`) by tracing engine internals; the contracts they exposed are now documented in scenario comments rather than as bug files, since they're correct-behavior gotchas rather than engine defects:

- `JetCompactA` requires the source DB to be **attached** before the call (engine path: `comp.cxx:214` `ErrCMPOpenDB` → `ErrDBOpenDatabase`).
- `JetSetColumn` with `JET_bitSetOverwriteLV` needs `JET_SETINFO.itagSequence = 1` so the engine targets the existing LV (engine: `lv.cxx:2501` `fNewInstance = (0 == itagSequence)`).
- `JetRestoreInstanceA` takes the handle by value, leaves the instance uninitialised on exit, and (with `szDest = nullptr`) restores to the database's **original** path — caller must `JetCreateInstance2A` first, set params, `JetRestoreInstanceA(handle, src, dest, nullptr)`, then `JetInit`.
