# ESE Linux port — roadmap

Last refreshed: 2026-05-12 (commit `04bd25e`).

The aim of the port is a working Linux ESE: `libese.so` plus consuming applications that can create, open, write, read, and close JET databases through the same JET API surface Windows ESE exports.  Reading Windows-produced `.edb`/`.log` files is **not** in scope for the initial release (see `~/.claude/projects/-p-ese-repo/memory/project_port_scope.md`); the on-disk format is allowed to diverge between platforms.

## Where the port stands today

Build & link surface — solid.
- Clang 22 toolchain at `/apps/clang-22.1.3`, libc++/libc++abi/libunwind statically linked, `-std=c++20 -fshort-wchar -fvisibility=hidden`.
- `libese.so` and 12 consumer binaries (`BookStoreSample`, `CcLayerUnit`, `COLLECTIONUnit`, `ERRVALIDATOR`, `EseLibWithTestsRunner`, `ese-tests`, `eseutil`, `IterQueryUnit`, `nls_smoke`, `RESMGRUNIT`, `STATUNIT`, `SYNCUNIT`) build clean in both Debug and Release.  Zero compiler warnings.
- Third-party deps wired: io_uring, libnls (vendored submodule under `third_party/libnls/`), libucl (vendored under `third_party/libucl-0.9.4/`), zstd 1.5.7 (vendored under `third_party/zstd-1.5.7/`).
- Build modes: Debug at `build/`, Release at `build-release/`.  Both pass the suite.

Test surface — extensive and green.
- `EseLibWithTestsRunner` tier-1 (no `-d`, runs JETUNITTEST): default-on suite passes cleanly with 35 opt-in tests skipped.  Release matches Debug.
- `EseLibWithTestsRunner` tier-2 (`-d <dir>`, runs JETUNITTESTDB against a real JET database): 32 tests pass; 776 opt-in tests skipped.
- `devlibtest` (7 binaries: `CcLayerUnit`, `COLLECTIONUnit`, `ERRVALIDATOR`, `IterQueryUnit`, `RESMGRUNIT`, `STATUNIT`, `SYNCUNIT`): tens of millions of assertions across the OS, sync, resmgr, and collection layers.  All passing.  `RESMGRUNIT` alone runs 2.1M tests; `SYNCUNIT` 2.5M.
- `ese-tests` (new — public-JET-API integration scenarios at `integration-tests/`): 125 scenarios across DDL, DML, navigation, transactions, long values, multi-values, escrow, temporary tables, sessions, backup/restore, recovery (including SIGKILL crash), snapshots, maintenance (compact), schema, limits, errors, scale, and concurrency.  Passes in Debug (23s) and Release (15s).
- All 7 originally-Windows-only test files (`daehelpers_test`, `osu_test`, `oslayer_test`, `logprereader_test`, `fmp_test`, `node_test`, `rbscleaner_test`) compile and pass on Linux.

Major plumbing pieces in place.
- `windows-shim/` (Win32 API surface) covers everything `osposix` + the engine reach for, including a real `__try`/`__except` via SIGSEGV + `sigsetjmp` (see `windows-shim/excpt.h` + `dev/ese/src/os/posix/winapi_seh.cxx`).
- Win32 NLS surface served by `libnls` (LGPL, vendored).  Replaces ICU.  Byte-compatible with Win32 (Wine-derived implementation against the same `.nls` data tables), keeping the door open for future on-disk format compat.
- Config layer (`config_posix.cxx`) reads `/etc/ese.conf` + `<exe>.ese.conf` via libucl — the registry-equivalent on Linux.
- `FormatMessageW` works against an mc-generated message table (`tools/mc/` C# native-AOT generator produces `jetmsgex_table.cxx`).
- File I/O: upstream `osfile.cxx` + `osfs.cxx` + `osdisk.cxx` now drive the live path.  The earlier parallel posix shims (`osdisk_posix.cxx`, `syncfile_posix.cxx`, the posix-specific `osfile`/`osfs` rewrites) have been retired.  `CIoUringFile` lives under the upstream `IFileAPI`, and IOREQ pool submissions route through io_uring (or the fall-back ReadFile/WriteFile path on filesystems without async support).
- Block-device identity wired against `/sys/block/<dev>/queue/rotational` + storage IOCTLs through `winapi_blockdev.cxx` and `winapi_blockdev_ioctl.cxx`, with a fallback for filesystems whose backing device isn't visible under `/sys`.
- Compression: Rtl* family backed by zstd 1.5.7 via `winapi_compression.cxx`.  Smoke test covers the journal compression round trip.
- Consumer entry points: `JetPlatformInitialize` / `JetPlatformTerminate` were added so `libese.so` consumers (eseutil, BookStoreSample, third-party callers) get the OS layer pre-init done for them.

Known gaps that block "real users."
- **No end-to-end smoke binary that doubles as a regression net.**  `BookStoreSample` boots the engine but exits with sample-level `-1047`; we have framework-driven coverage in `ese-tests` and `EseLibWithTestsRunner` but no standalone `samples/jet_smoke.cxx`-style program that proves a clean `CreateInstance → Init → CreateDatabase → insert → reopen → verify → Term` round trip and fails loud in CI when the libese.so init chain breaks again.
- **Encryption is stubbed.**  `dev/ese/src/os/posix/encrypt_posix.cxx` returns `JET_errFeatureNotAvailable` for `ErrOSEncryptWithAes256`/`ErrOSDecryptWithAes256`/`ErrOSCreateAes256Key`; CRC32C and size-math kept portable.  Engine works for any consumer that doesn't enable encryption at rest.
- **Some `winapi_*.cxx` functions remain unexercised** — surface that compiles and links but no test or live code path touches yet.  Not a gating problem; flagged for the eventual API-coverage audit (priority #4 below).
- **ETW is no-op stubs.**  Eventual target is LTTng UST; not started.
- **Event log is `DISABLE_EVENT_LOG`.**  Currently the `_etguidEventLogInfo/Warn/Error` path routes to stderr for visibility; syslog/journald is the eventual real backend.
- **Release isn't the default.**  Both build modes work, but the project layout still leans on `build/` being the canonical Debug tree.  Promoting Release to default is a config tweak, not new code.

## Priority list

### 1. End-to-end "minimum viable database" smoke binary

A small standalone program — `samples/jet_smoke.cxx` is the natural home, or a revived/repurposed `BookStoreSample` — that demonstrates the whole JET round trip:

```
JetCreateInstance(...) → JetSetSystemParameter(SystemPath/TempPath/LogFilePath)
→ JetInit(...) → JetBeginSession → JetCreateDatabase → JetCreateTable
→ JetAddColumn → JetOpenTable → JetPrepareUpdate(prepInsert)
→ JetSetColumn (a few rows) → JetUpdate
→ JetEndSession → JetTerm(...)
→ reopen, verify rows survived restart, close.
```

The point is a binary anyone can run by hand to see Linux ESE work — without `EseLibWithTestsRunner`'s tier-1/tier-2 init shims and without the `ese-tests` framework's per-scenario harness.  Both of those tools mask init bugs that real consumers would hit.  The standalone smoke should also be wired into CMake so a future regression in the libese.so init chain trips the build, not a downstream user.

### 2. Encryption: libsodium-backed AES-256

The last meaningful runtime stub.  Replace `JET_errFeatureNotAvailable` in `encrypt_posix.cxx` with libsodium-backed AES-256.  Since on-disk format compat isn't required, AES-256-GCM (libsodium's high-level `crypto_aead_aes256gcm_*` API) is fine if CBC bit-compat with Windows turns out to be friction; CBC is the default for compat-friendliness but we're not chasing it.

Sequence (matches the "tests over runtime" rule):

1. Unit test the new encrypt path under `dev/ese/src/_devlibtest/` — round-trip a known plaintext, verify failure modes match `JET_err*`.
2. Wire libsodium into the CMake graph (pkg-config probe, fall back to vendored if not available).
3. Replace the stubs in `encrypt_posix.cxx`.
4. Run tier-1 + tier-2 with an encrypted-database scenario.

### 3. Unify the `ErrorIOMgrIssueIO` / `GetOverlappedResult_` paths

`osdisk.cxx` had four `#ifdef ESE_OS_WINDOWS` gates.  Three were closed in the same pass that landed disk-info, queue-depth, and SMART shims:

- `LoadDiskInfo_` / `LoadCachePerf_` now compile on both platforms; the Linux dispatcher fills `STORAGE_DEVICE_DESCRIPTOR` (vendor/model/firmware/serial from `/sys/block/<dev>/device/{vendor,model,rev,firmware_rev,serial}`), `STORAGE_ADAPTER_DESCRIPTOR` (with NVMe vs SATA inferred from the disk name), and reports `ERROR_NOT_SUPPORTED` for `SMART_GET_VERSION` / `SMART_RCV_DRIVE_DATA` / `StorageDeviceCopyOffloadProperty` so the engine's degradation paths take over.
- `ErrInitDisk` now opens `\\.\PhysicalDriveN` on Linux via the same shim path Windows uses.  The shim decodes N as a makedev-encoded `(major:minor)` (matching what `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` packs), resolves it through `/sys/dev/block/<major>:<minor>`, and fails the open when no real device backs the number — which preserves the engine's "couldn't determine disk identity, treat as HDD" path that unit tests with synthetic dwDiskNumber values rely on.
- `QueryDiskPerformance` reads queue depth through `IOCTL_DISK_PERFORMANCE`, which the shim backs with `/proc/diskstats` (field 12, `in_flight`).

The remaining gate (`osdisk.cxx:6390..6797`) wraps `GetOverlappedResult_` and the Windows `ErrorIOMgrIssueIO` body — the Win32 path uses `OVERLAPPED` + IOCP and waits via `GetOverlappedResult_`; the Linux path submits async IOs through io_uring (`OSPosixIouringSubmitIOREQ`) or runs sync IOs through pread/pwrite-backed `ReadFile`/`WriteFile`.  This is a genuine architectural divergence in IO submission, not a missing shim — keeping it as a gate is the right shape per the "last resort" exception in `~/.claude/projects/-p-ese-repo/memory/feedback_port_extend_shim_not_gate.md`.

What's worth doing here:

- **Mirror the Win32 pre/post amble onto the Linux body.**  The Windows path runs `RFSAlloc`, `ErrFaultInjection(17384/64738/42980)`, and `UtilThreadBeginLowIOPriority`/`UtilThreadEndLowIOPriority` around the submit; the Linux body skips them.  Lifting these into the Linux arm (or into a shared helper) would close the testable-behavior gap between the two arms while still allowing the submit step itself to diverge.
- **Optional: extend `ReadFile`/`WriteFile` to honour an `OVERLAPPED.hEvent` async contract via io_uring.**  Then both arms could share most of the dispatch, and the gate would shrink to just the iomethod switch.  Bigger change; defer unless we end up wanting it for some other reason.

### 4. JET public API coverage inventory + ratcheting

`ese-tests` is at 125 scenarios but coverage isn't measured against the full `JetXxx` surface.  Worth doing once:

- Enumerate every `JetXxx` exported function from `dev/ese/src/inc/jet.h`.
- Grade each as: exercised by `ese-tests`, exercised by tier-1/2 only, not exercised, or known-broken.
- File gaps as scenarios in `ese-tests`.
- Promote any scenario that uncovers a real engine bug to a permanent regression (the Phase 6 Compact/OverwriteLV/Restore fixes were exactly this pattern).

This is also where to fold in the "untouched winapi_*.cxx functions" audit — anything the engine never calls is dead surface we can either delete or stub down.

### 5. Default to Release, capture Release perf baseline

Both build modes work.  What's left is cosmetic + measurement:

- Default CMake to `Release` unless `-DCMAKE_BUILD_TYPE=Debug` is explicitly passed.
- Run perf-flagged tests (`CPAGE.ReplacePerf`, `CHECKSUM.Perf`, the iouring micro-bench) in Release and pin the numbers somewhere durable.  These were captured for Debug at `31a90b1`; Release deltas haven't been written down.
- Add a Release smoke target to whatever CMake-level smoke (#1) ends up looking like.

### 6. ETW → LTTng UST

Currently every ETW emitter is a no-op stub.  The `_etguidEventLog*` family is special-cased to route to stderr (so `JET_EventLoggingLevelMax` actually produces visible output).  Eventual target is LTTng UST: ABI-compatible enough with ETW that the indirection layer can be straightforward.  Defer until tracing engine behavior in production is on the table.

### 7. Real event-log backend (syslog/journald)

`DISABLE_EVENT_LOG` is defined; events go nowhere (except the stderr-routing case above).  The existing `OSEventReportEvent` chokepoint is the natural place to plug in syslog (`syslog(3)`) or systemd-journal (`sd_journal_send`).  Defer until #1 lands — there's no point logging events if no real binary is yet a real consumer.

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
