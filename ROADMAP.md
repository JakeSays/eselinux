# ESE Linux port — roadmap

Last refreshed: 2026-05-12 (commit `31a90b1`).

The aim of the port is a working Linux ESE: `libese.so` plus consuming applications that can create, open, write, read, and close JET databases through the same JET API surface Windows ESE exports.  Reading Windows-produced `.edb`/`.log` files is **not** in scope for the initial release (see `~/.claude/projects/-p-ese-repo/memory/project_port_scope.md`); the on-disk format is allowed to diverge between platforms.

## Where the port stands today

Build & link surface — solid.
- Clang 22 toolchain at `/apps/clang-22.1.3`, libc++/libc++abi/libunwind statically linked, `-std=c++20 -fshort-wchar -fvisibility=hidden`.
- `libese.so` and 11 consumer binaries (devlibtest exes, `eseutil`, `BookStoreSample`, `EseLibWithTestsRunner`, `nls_smoke`) all build clean.  Zero compiler warnings.
- Third-party deps wired: io_uring, libnls (vendored submodule under `third_party/libnls/`), libucl (vendored under `third_party/libucl-0.9.4/`).
- Build mode: DEBUG only.  No Release configuration exercised.

Test surface — extensive and green.
- `EseLibWithTestsRunner` tier-1 (no `-d`, runs JETUNITTEST): 575 default-on + 18 opt-in, all passing.
- `EseLibWithTestsRunner` tier-2 (`-d <dir>`, runs JETUNITTESTDB against a real JET database): 35/35.
- `devlibtest` (7 binaries): ~59M assertions across the OS, sync, resmgr, and collection layers.  All passing.
- All 7 originally-Windows-only test files (`daehelpers_test`, `osu_test`, `oslayer_test`, `logprereader_test`, `fmp_test`, `node_test`, `rbscleaner_test`) compile and pass on Linux.

Major plumbing pieces in place.
- `windows-shim/` (Win32 API surface) covers everything `osposix` + the engine reach for, including a real `__try`/`__except` via SIGSEGV + `sigsetjmp` (see `windows-shim/excpt.h` + `dev/ese/src/os/posix/winapi_seh.cxx`).
- Win32 NLS surface served by `libnls` (LGPL, vendored).  Replaces ICU.  Byte-compatible with Win32 (Wine-derived implementation against the same `.nls` data tables), keeping the door open for future on-disk format compat.
- Config layer (`config_posix.cxx`) reads `/etc/ese.conf` + `<exe>.ese.conf` via libucl — the registry-equivalent on Linux.
- `FormatMessageW` works against an mc-generated message table (`tools/mc/` C# native-AOT generator produces `jetmsgex_table.cxx`).
- io_uring backs the synchronous file API via `CIoUringFile` in `syncfile_posix.cxx`.

Known gaps that block "real users."
- **`eseutil` startup regression** — running it gets `"Out of memory error during OS Layer pre-init."` (`COSLayerPreInit::FInitd()` returns false).  Was working at the 2026-05-07 milestone; some commit between then and now broke whatever startup chain it relies on (likely the static-ctor `g_oslayerLibeseInit` in libese.so/std.cxx).  `BookStoreSample` regressed in the same family: `Assert(g_cbUserTLSSize > 0)` at `thread.cxx:344`.  `EseLibWithTestsRunner` sidesteps both by explicitly calling `OSPrepreinitSetUserTLSSize(sizeof(TLS))` + `ErrOSUInit()` from its tier-1 entry point.
- **No end-to-end demonstration.**  We have framework-driven tests but no example program that does `JetCreateInstance → JetInit → JetCreateDatabase → JetOpenTable → insert rows → close → exit cleanly`.
- **`osdisk` layer is a thin stub** — see priority #3 below.
- **Stubs still in place** for: Rtl compression (`STATUS_NOT_IMPLEMENTED`, see `~/.claude/projects/-p-ese-repo/memory/project_port_compression_todo.md`); encryption (libsodium chosen, not wired); a handful of `winapi_*.cxx` functions nothing has exercised yet.
- **DEBUG-only.**  `NDEBUG` paths in the engine are untested.
- **ETW is no-op stubs.**  Eventual target is LTTng UST; not started.
- **Event log is `DISABLE_EVENT_LOG`.**  Currently the `_etguidEventLogInfo/Warn/Error` path routes to stderr for visibility; syslog/journald is the eventual real backend.

## Priority list

### 1. Fix the `eseutil` + `BookStoreSample` regression

Any libese.so consumer that does the conventional thing (`COSLayerPreInit oslayer;` on the stack at the top of `main`, expect `g_oslayerLibeseInit` to have pre-inited the OS layer) hits this wall right now.  This is the most immediate blocker for "demonstrate a real consumer of the engine."

- Run `eseutil` under gdb, set a breakpoint at `COSLayerPreInit::FInitd`, walk back to see what state isn't getting set up.
- Most likely culprit: a change to `g_oslayerLibeseInit` ctor ordering, or an init path that previously fell through to a sensible default but now needs an explicit `OSPrepreinitSetUserTLSSize` call.
- Once the static-init chain is fixed, both eseutil and BookStoreSample should come back without per-binary changes.
- Add a CMake-level smoke step that runs `eseutil` with no args and checks the exit; that catches future regressions in this layer.

### 2. End-to-end "minimum viable database" smoke

A small program — could be a new test binary, or a revival of `BookStoreSample`, or a fresh `samples/jet_smoke.cxx` — that demonstrates the whole JET round-trip:

```
JetCreateInstance(...) → JetSetSystemParameter(SystemPath/TempPath/LogFilePath)
→ JetInit(...) → JetBeginSession → JetCreateDatabase → JetCreateTable
→ JetAddColumn → JetOpenTable → JetPrepareUpdate(prepInsert)
→ JetSetColumn (a few rows) → JetUpdate
→ JetEndSession → JetTerm(...)
→ reopen, verify rows survived restart, close.
```

This is the demo that proves Linux ESE works — not as a test framework but as a database engine.  Should be invokable from `EseLibWithTestsRunner -d <dir>` (tier-2 already proves JetInit/JetTerm work) and also as a standalone exe.  The standalone version doubles as a regression net for #1 — if anything in the startup chain breaks again, the smoke fails loud.

### 3. Complete `osdisk.cxx` implementation

Currently `dev/ese/src/os/posix/osdisk_posix.cxx` is a 241-line shim against the upstream 9670-line `dev/ese/src/os/osdisk.cxx`.  What we have:

- QoS ↔ urgent-level math helpers (verbatim from upstream — pure bit twiddling)
- `OSFileIIOReportError` for emitting JET events on failed IOs
- `CioDefaultUrgentOutstandingIOMax`, `CioBackgroundIOLow`, `FlightConcurrentMetedOps` — engine-facing knobs
- No-op `ErrOSDiskIOREQReserve` / `OSDiskIOREQUnreserve`
- Empty `FOSDiskPreinit` / `OSDiskPostterm`

What we don't have:

- **No IOREQ pool.**  Upstream maintains a pool of in-flight IO request descriptors so callers can guarantee a slot is available before issuing.  Our `syncfile_posix.cxx` allocates an `osposix::IOContext` per IO instead, so there's no global ceiling and no reserve-then-issue ordering.
- **No `COSDisk` class.**  Upstream models each physical disk with its own queue + scheduler that:
  - Tracks outstanding IO depth per disk
  - Dispatches IOs in QoS order (urgent → background) rather than submission order
  - Implements the "smooth ramp" of urgent-level → max-outstanding-IO using the helpers we already kept
  - Distinguishes metered ops (rate-limited) from regular and urgent ops
  - Applies backpressure (delays / serializes) when the queue is deep
- **No IO completion thread / dispatcher.**  Upstream has a dedicated thread that drains IOCP completions and routes them to caller-supplied handoff/complete callbacks.  Our `iouring_posix.cxx` does completions inline; we'd need to integrate disk-level queueing on top of that.
- **No disk identification.**  Upstream maps file handles to physical disks (rotating vs SSD detection, per-disk queue selection).  Linux exposes the same data via `/sys/block/<dev>/queue/rotational` (already noted in `syncfile_posix.cxx`); we'd need to plumb it.
- **No read combining / write coalescing.**  Upstream merges adjacent IOs.  Engine call sites (block cache, log writer) expect this to amortize syscall + iouring submission overhead.
- **No latency tracking / abnormal-latency event emission.**  Upstream's `IFilePerfAPI` records per-IO latency and emits events at the 60s threshold (`dtickOSFileAbnormalIOLatencyEvent` is defined but the recording path is partial).
- **`PatrolDogSynchronizer`** — upstream provides a watchdog that detects when an IO has been outstanding too long.  Already there in source (it's in `osdisk.cxx` and the `PatrolDogSynchronizer.*` tests pass), but the disk-side hook to actually invoke it on stuck IOs isn't wired.

For tier-1/tier-2 unit tests this doesn't matter — they hit `CIoUringFile` directly and the absence of queueing isn't visible.  For real-world workloads it matters a lot: every IO from every fiber races into io_uring with no ordering, no rate-limit, no priority.  ESE's checkpoint logic, log writer, and block cache eviction strategy all assume the engine can express priorities and the OS layer will honor them.

Roughly, the work is:

1. **IOREQ-equivalent pool.**  Either repurpose the upstream `IOREQ` struct against io_uring, or build a parallel `LinuxIOREQ` with the fields engine code reads.
2. **`COSDisk`** equivalent with QoS-ordered submission.  Probably one COSDisk per minor-device (extracted via `stat()` + `/sys/dev/block/<major>:<minor>/queue/rotational`).  Internal queue is a small priority structure (urgency buckets, FIFO within bucket).  Drain into io_uring at the configured concurrency.
3. **IO completion handoff** — `CIoUringFile`'s submit calls give the IOContext to COSDisk; on io_uring completion, COSDisk routes to the per-IO completion handler.
4. **Latency recording** in the `IFilePerfAPI` already attached to each `CIoUringFile`.  Update `m_pfpapi`'s latency stats on completion; emit the abnormal-latency event when threshold is crossed.
5. **PatrolDog wiring** — register each in-flight IO with the PatrolDogSynchronizer; the existing watchdog tests (`PatrolDogSynchronizer.*` in `oslayer_test.cxx`) should immediately start exercising the live path.

Estimated scope: probably 1500–2500 LOC in `osdisk_posix.cxx` (vs the upstream's 9670, much of which is IOCP-specific orchestration we don't need).  Should land behind a feature flag at first so we can A/B against the current direct-submit path on the test suite before flipping over.

### 4. Release build configuration

Currently CMake's build type is implicitly DEBUG everywhere.  `NDEBUG` paths in the engine — including some `Enforce`/`Expected` macros, perf-critical inlines, and CRT assertions — are entirely untested on Linux.

- Add a `Release` config (`CMAKE_BUILD_TYPE=Release`) with `-O2 -DNDEBUG`.  Make it pass the test suite.
- Run the perf tests (`CPAGE.*Perf`, `CHECKSUM.Perf`) in Release and capture numbers.  Compare to DEBUG baseline (already gathered as of commit `31a90b1`).
- Run devlibtest in Release.  Both modes should pass.
- Once Release works, default the build to Release unless `-DCMAKE_BUILD_TYPE=Debug` is passed.

### 5. libsodium-backed encryption + zstd-backed compression

Two pre-decided tech choices that are still stubs:

- `dev/ese/src/os/posix/winapi_compression.cxx` returns `STATUS_NOT_IMPLEMENTED` for the `Rtl*` family.  Engine call sites in `_journalentry.hxx` fall back to uncompressed.  Replace with zstd (`libzstd` via pkg-config).  See `~/.claude/projects/-p-ese-repo/memory/project_port_compression_todo.md` for the integration sketch.
- Encryption (AES-256-CBC) is currently stubbed in `encrypt_posix.cxx`.  Wire libsodium.  Since on-disk format compat isn't required, AES-256-GCM (libsodium's high-level API) is an acceptable alternative if CBC bit-compat with Windows turns out to be friction.

Bring each up behind a small unit test in the relevant test file before touching the live engine path — per the "tests over runtime" feedback the user has established.

### 6. ETW → LTTng UST

Currently every ETW emitter is a no-op stub.  The `_etguidEventLog*` family is special-cased to route to stderr (so `JET_EventLoggingLevelMax` actually produces visible output).  Eventual target is LTTng UST: ABI-compatible enough with ETW that the indirection layer can be straightforward.  Defer until tracing engine behavior in production is on the table.

### 7. Real event-log backend (syslog/journald)

`DISABLE_EVENT_LOG` is defined; events go nowhere (except the stderr-routing case above).  The existing `OSEventReportEvent` chokepoint is the natural place to plug in syslog (`syslog(3)`) or systemd-journal (`sd_journal_send`).  Defer until #1 and #2 are done — there's no point logging events if the engine isn't running real workloads yet.

### 8. JET public API coverage inventory

Not actually a port-completion item, but worth doing once: enumerate every `JetXxx` exported function and grade what's exercised by tests vs untested vs known-broken.  Gives us a coverage baseline we don't currently have.

## Things explicitly out of scope (still)

- **Windows `.edb` / `.log` format compatibility.**  Deferred indefinitely.  libnls keeps the door open by producing Windows-byte-compatible sort keys, but the disk format on Linux can otherwise diverge freely (struct packing, ICU vs NLS bytes were the original concerns; both are addressed in a way that allows divergence).
- **VSS-equivalent backup integration.**  `noncore/eseshadow` is dropped from the Linux build.  Linux backups happen at the filesystem layer (LVM/Btrfs/ZFS snapshots), not via app-coordinated quiescence.
- **WinDbg extension `edbg.cxx`.**  21k LOC of `dbgeng.h` COM walking; pure dev-triage tool.  A GDB/LLDB Python equivalent is a future separate project, not a port deliverable.
- **GCC support.**  Clang only.  `-fms-extensions` + `-fdeclspec` + `-fdelayed-template-parsing` is Clang-only.

## What's in `bugs/`

- `bugs/cpage-ctagreserved-missing-mask.md` — upstream bug found while bringing up the `node_test.cxx` fuzz suites.  `PGHDR::itagState`'s "high bit reserved for future use" wasn't actually being masked at read sites; fixed in `c00f86b` + `5ccc4b1`.  Open question for Microsoft when filed upstream.
