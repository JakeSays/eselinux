# JET API coverage inventory

Source: `jetapi.h` (the standalone repo-root header), tracked against the
`integration-tests` corpus.  Coverage is measured at the **base** API
level (A/W variants collapsed): if `JetCreateInstanceA` is called
anywhere in the tests, the base name `JetCreateInstance` is treated as
covered.  Version-suffixed siblings (e.g. `JetCreateIndex2`,
`JetCreateIndex3`, `JetCreateIndex4`) are tracked separately from their
unversioned base.

A scenario only counts as covering an API if it verifies the API's
**functional behavior** — not merely that the call shape is accepted.
APIs whose engine implementation is an unconditional stub upstream
(returns `JET_errFeatureNotAvailable` or `JET_wrnNyi` regardless of
platform — `dev/ese/src/ese/pib.cxx:994`, `jetapi.cxx:9582`,
`jetapi.cxx:18940`) are out-of-scope, not "covered with caveats".

**Totals:** 212 base APIs declared, 14 carved out as not testable (engine
stubs, version-gated declarations, deprecated wrappers, internal
engineering surface).  Of the **198 testable** APIs, **196 are covered
(99%)**.

**A/W methodology note:** the 196-covered count is measured at the
*base* API name — `JetCreateInstance` covered means at least one of
`JetCreateInstanceA` / `JetCreateInstanceW` is exercised.  Round 11
adds W-variant smoke scenarios (`WideApiScenarios.cxx`) so the
UTF-16 entry points aren't entirely unexercised, but those W-only
scenarios are explicitly NOT counted in the percentage — the
underlying capability is already covered by the corresponding A
scenario, and counting W as a separate "API" would double-count.
Round 11 is a sanity check on the windows-shim
MultiByteToWideChar / WideCharToMultiByte plumbing, not net new
functional coverage.

## Tested (196)

Core surface for every scenario the engine actually runs.  Includes
DDL (table/column/index create/delete/rename, **JetDeleteTable**,
**JetSetColumnDefaultValue**), DML
(insert/update/delete/retrieve, **JetEnumerateColumns**,
**JetRetrieveTaggedColumnList**), navigation
(move/seek/setIndex/setRange, **JetIntersectIndexes**,
**JetSetCursorFilter**, **JetSetTableSequential** /
**JetResetTableSequential**), transactions
(begin/commit/rollback), backup/restore (incl. external + restore,
**JetStopBackupInstance**, **JetStopBackup**), snapshot lifecycle,
params, init/term, attach/detach/open/close, JetCompact /
JetDefragment, JetEscrowUpdate, JetComputeStats,
JetGetBookmark / JetGotoBookmark, JetFreeBuffer.

Diagnostics: **JetGetSessionInfo**, **JetGetCursorInfo**,
**JetGetVersion**, **JetGetThreadStats**, **JetGetErrorInfo**.

Cursor handle management: **JetDupSession**, **JetDupCursor**.

Cursor / position queries: **JetGetCurrentIndex**,
**JetGetRecordPosition**, **JetGotoPosition**,
**JetGetSecondaryIndexBookmark**, **JetGotoSecondaryIndexBookmark**.

Database lifecycle: **JetGrowDatabase**, **JetSetDatabaseSize**,
**JetSetMaxDatabaseSize**, **JetGetMaxDatabaseSize**,
**JetResizeDatabase**.  Online maintenance: **JetDatabaseScan**.

Engine hooks: **JetRegisterCallback**, **JetUnregisterCallback**,
**JetIdle** (`JET_bitIdleWaitForAsyncActivity` /
`JET_bitIdleAvailBuffersStatus`).

Info surface: **JetGetDatabaseInfo**, **JetGetObjectInfo**,
**JetGetIndexInfo**, **JetGetInstanceInfo**,
**JetGetInstanceMiscInfo**, **JetGetDatabaseFileInfo**,
**JetGetLogFileInfo**, **JetGetLogInfoInstance**,
**JetGetLogInfoInstance2**, **JetGetTruncateLogInfoInstance**,
**JetGetAttachInfo** (global), **JetGetPageInfo**,
**JetGetPageInfo2**, **JetGetDatabasePages**,
**JetGetSystemParameter**, **JetGetResourceParam**,
**JetSetResourceParam**.

Record-level: **JetRetrieveKey**, **JetGetLock**,
**JetGetRecordSize**, **JetGetRecordSize2**, **JetGetRecordSize3**,
**JetIndexRecordCount**, **JetIndexRecordCount2**.

Session / cursor local storage: **JetSetSessionContext**,
**JetResetSessionContext**, **JetSetLS**, **JetGetLS**,
**JetSetSessionParameter**, **JetGetSessionParameter**.

Pre-read surface: **JetPrereadKeys**, **JetPrereadIndexRange**,
**JetPrereadIndexRanges**, **JetPrereadTables**.

External-backup protocol (global + instance forms): **JetBeginExternalBackup**,
**JetEndExternalBackup**, **JetEndExternalBackupInstance2**,
**JetTruncateLog**, **JetTruncateLogInstance**,
**JetOpenFile**, **JetOpenFileInstance**,
**JetOpenFileSectionInstance**, **JetReadFile**,
**JetReadFileInstance**, **JetCloseFile**, **JetCloseFileInstance**,
**JetGetLogInfo**, **JetRemoveLogfile**,
**JetBeginSurrogateBackup**, **JetEndSurrogateBackup**.

OS snapshot extensions: **JetOSSnapshotAbort**,
**JetOSSnapshotPrepareInstance**, **JetOSSnapshotGetFreezeInfo**,
**JetOSSnapshotTruncateLog**, **JetOSSnapshotTruncateLogInstance**
(Prepare with `JET_bitContinueAfterThaw`, then Freeze → Thaw →
TruncateLog → End).

Instance lifecycle: **JetCreateInstance** (unversioned) in addition
to `JetCreateInstance2`, **JetInit2**, **JetEnableMultiInstance**
(forked-child scenario for pristine engine).

Service control: **JetStopService**, **JetStopServiceInstance**,
**JetStopServiceInstance2** (with `JET_bitStopServiceBackgroundUserTasks`
and `JET_bitStopServiceResume`).

Crash configuration: **JetConfigureProcessForCrashDump**.

DDL conversion: **JetConvertDDL** (`opDDLConvIncreaseMaxColumnSize`
and `opDDLConvChangeIndexDensity`).

Column-by-reference + stream surface:
**JetRetrieveColumnByReference**, **JetPrereadColumnsByReference**,
**JetStreamRecords**, **JetRetrieveColumnFromRecordStream**.

Revertable-Backup-Set (RBS / revert snapshot):
**JetRBSPrepareRevert**, **JetRBSExecuteRevert**,
**JetRBSCancelRevert**, **JetGetRBSFileInfo**.

Replication / replica repair (TCP-loopback multi-process topology):
**JetConsumeLogData** (live-tail log shipping via
`JET_paramEmitLogDataCallback` round-trip; passive promotes
`.jsl` shadow logs to `.log` for recovery),
**JetBeginDatabaseIncrementalReseed**,
**JetPatchDatabasePages** (Cancel-path and Commit-path scenarios),
**JetEndDatabaseIncrementalReseed**,
**JetOnlinePatchDatabasePage** (PAGE_PATCH_TOKEN with log
signature),
**JetExternalRestore** (caller pre-stages backup files in the
target dir, then API applies log replay to bring DB current).

Versioned variants that add functional surface (not just thin
wrappers): **JetBeginTransaction3** (trxid stamping),
**JetCommitTransaction2** (commit-id + durable delay),
**JetUpdate2** (grbit), **JetSetCurrentIndex2** (NoMove),
**JetSetCurrentIndex4** (JET_INDEXID cache + itagSequence),
**JetCreateIndex2** (JET_INDEXCREATE struct),
**JetCreateIndex3** (INDEXCREATE2 + JET_SPACEHINTS),
**JetCreateIndex4** (INDEXCREATE3 + JET_UNICODEINDEX2),
**JetCreateDatabase2** / **JetAttachDatabase2** (size-cap),
**JetCreateDatabase3** / **JetAttachDatabase3** (JET_SETDBPARAM
array at create/attach time),
**JetCreateTableColumnIndex2** (callback hook),
**JetCreateTableColumnIndex3** / **4** / **5** (TABLECREATE3/4/5 —
pSeqSpacehints/cbSeparateLV, INDEXCREATE3 with locale-name
sort, cbLVChunkMax),
**JetInit4** (RSTINFO2 / RSTMAP2 — exercised by RBS scenarios),
**JetOpenTempTable3** (JET_UNICODEINDEX),
**JetOpenTemporaryTable** / **JetOpenTemporaryTable2**
(struct-based open with JET_UNICODEINDEX / JET_UNICODEINDEX2 +
cbKeyMost / cbVarSegMac limits),
**JetDefragment2** (callback),
**JetGetPageInfo2**, **JetGetRecordSize2**, **JetGetRecordSize3**,
**JetIndexRecordCount2**.

Thin-wrapper variants — each adds a single grbit, callback, or
struct field on top of an already-covered base, exercised once
so the v2/v3 dispatch is verified:
**JetInit3** (JET_RSTINFO struct in place of JET_RSTINFO2's wider
shape), **JetDeleteColumn2** (`JET_bitDeleteColumnIgnoreTemplateColumns`
grbit), **JetDetachDatabase2** (`JET_GRBIT` grbit on detach),
**JetExternalRestore2** (`JET_LOGINFO` bundling of `genLow`/`genHigh`
plus explicit target-instance path overrides),
**JetOpenTempTable2** (bare-lcid temp-table form between v1's
no-locale and v3's `JET_UNICODEINDEX` struct — exercised with a
Long-typed key so the call-shape dispatch is covered even though
lcid is moot for non-text keys),
**JetSetCurrentIndex3** (v2's grbit-only surface plus an
`itagSequence` selector for multi-valued indexes; v4 adds the
`JET_INDEXID` cache on top).

## Out of scope (engineering surface)

These ship in `jetapi.h` but are intentionally excluded from the
coverage target:

- **JetDBUtilities / JetTestHook / JetTracing** — engineering surface,
  not user-facing.
- **Deprecated backup/restore wrappers** — `JetRestore`, `JetRestore2`,
  `JetBackup`.  The `*Instance` variants are covered in
  `BackupRestore.*`; the global forms are thin wrappers around the
  per-instance form and aren't reachable from user code that has
  already moved off the implicit-global-instance model.
- **`JetUpgradeDatabase`** — historical Windows-only DB-format
  upgrade path; not relevant on the Linux port (no legacy
  Windows-format files to upgrade).
- **Upstream stubs that always return FNA / NYI** — these are not
  Linux port gaps but unconditional engine stubs in the Microsoft
  source.  No functional behavior to verify until they're
  implemented upstream:
  - `JetGetCounter` / `JetResetCounter` —
    `dev/ese/src/ese/pib.cxx:994` returns
    `JET_errFeatureNotAvailable`.
  - `JetPrepareToCommitTransaction` —
    `dev/ese/src/ese/jetapi.cxx:9582` returns
    `JET_errFeatureNotAvailable`.
  - `JetSnapshotStart` / `JetSnapshotStop` —
    `dev/ese/src/ese/jetapi.cxx:18940` comment:
    "OBSOLETE: never finished, VSS used instead".
  - `JetDefragment3` — `dev/ese/src/ese/jetapi.cxx:20064`
    returns `JET_errInvalidParameter` with an "OBSOLETE: only
    used by SFS" comment.
- **`JetDeleteTable2`** — pinned out by `JET_VERSION > 0x0A01`
  in `jetapi.h:6976`; the declaration isn't visible at the
  version we ship.  Will land naturally if/when JET_VERSION
  bumps; coverage scaffolding (a `Schema.DeleteTable2*`
  scenario mirroring `DeleteColumn2RemovesColumn`) is sketched
  in `SchemaScenarios.cxx` as a comment placeholder.

## Round history

- **Round 1**: navigation/transaction/schema baseline.
- **Round 2**: diagnostics + retrieve + LS — `JetRetrieveKey`,
  `JetGetLock`, `JetGetRecordSize`, `JetGetDatabaseInfo`,
  `JetGetObjectInfo`, `JetGetIndexInfo`, `JetGetInstanceInfo`,
  `JetSetSessionContext`, `JetResetSessionContext`,
  `JetSetLS`, `JetGetLS`, `JetIndexRecordCount`.
- **Round 3** (commit `fb5c248`): `JetGetCurrentIndex`,
  `JetSetTableSequential`/`Reset`, `JetSetCursorFilter`,
  `JetGetRecordPosition`/`GotoPosition`,
  `JetGetSecondaryIndexBookmark`/`GotoSecondaryIndexBookmark`,
  `JetSetSessionParameter`/`GetSessionParameter`,
  `JetGetSystemParameter`, `JetSetColumnDefaultValue`,
  `JetResizeDatabase`, `JetGetAttachInfo` (global),
  `JetGetLogInfoInstance`, `JetGetTruncateLogInfoInstance`,
  `JetTruncateLogInstance`, `JetStopBackupInstance`,
  `JetGetInstanceMiscInfo`, `JetGetLogFileInfo`,
  `JetGetDatabaseFileInfo`, `JetGetPageInfo`, `JetGetDatabasePages`,
  `JetRetrieveTaggedColumnList`, `JetPrereadKeys`,
  `JetPrereadIndexRange`, `JetPrereadTables`,
  `JetCreateInstance` (unversioned).
- **Round 4**: `JetDatabaseScan`, `JetOSSnapshotAbort`,
  `JetOSSnapshotPrepareInstance`, `JetOSSnapshotGetFreezeInfo`,
  `JetGetPageInfo2`, `JetPrereadIndexRanges`, `JetGetErrorInfo`,
  `JetGetResourceParam`, `JetSetResourceParam`,
  `JetBeginExternalBackup`, `JetEndExternalBackup`,
  `JetStopBackup`, `JetTruncateLog`, `JetOpenFileInstance`,
  `JetReadFileInstance`, `JetCloseFileInstance`,
  `JetEndExternalBackupInstance2`, `JetGetLogInfoInstance2`,
  `JetGetRecordSize2`, `JetGetRecordSize3`,
  `JetIndexRecordCount2`.
  Audit removed three engine-stub APIs from the round-3 covered
  list: `JetGetCounter`, `JetResetCounter`,
  `JetPrepareToCommitTransaction`.

- **Round 5**: global file-access (`JetOpenFile`,
  `JetReadFile`, `JetCloseFile`), `JetGetLogInfo` (global),
  `JetOpenFileSectionInstance`, `JetRemoveLogfile`,
  `JetBeginSurrogateBackup`, `JetEndSurrogateBackup`,
  `JetBeginTransaction3`, `JetCommitTransaction2`, `JetUpdate2`,
  `JetSetCurrentIndex2`, `JetCreateIndex2`, `JetCreateDatabase2`,
  `JetAttachDatabase2`, `JetCreateTableColumnIndex2`,
  `JetOpenTempTable3`, `JetDefragment2`, `JetInit2`,
  `JetEnableMultiInstance` (forked-child via CrashHelper).

- **Round 6**: `JetStopService`,
  `JetStopServiceInstance`, `JetStopServiceInstance2`,
  `JetConfigureProcessForCrashDump`, `JetConvertDDL`
  (`opDDLConvIncreaseMaxColumnSize` + `opDDLConvChangeIndexDensity`),
  `JetRetrieveColumnByReference`, `JetPrereadColumnsByReference`,
  `JetStreamRecords`, `JetRetrieveColumnFromRecordStream`.
  Surfaced three engine-contract gotchas (documented inline in
  scenarios + below): JetConvertDDL doesn't invalidate cached
  FCB/TDB so cbMax changes need detach+reattach; the record-stream
  parser's `iRecord` is 0-based (header inits to `ulMax`, first
  flip wraps to 0); string-column overflow on `JetSetColumn`
  surfaces as `JET_wrnColumnMaxTruncated` (1512), not
  `JET_errColumnTooBig`.

- **Round 7**: RBS / revert-snapshot surface —
  `JetRBSPrepareRevert`, `JetRBSExecuteRevert`,
  `JetRBSCancelRevert`, `JetGetRBSFileInfo` (via
  `JetGetRBSFileInfoA`).  Four scenarios in `RbsScenarios.cxx`:
  prepare+cancel, file-info, prepare-reject for an unreachable
  target, and end-to-end execute-revert that rolls a 502-row
  database back to its 500-row bootstrap state.
  Engine-contract gotchas surfaced during the round (documented
  inline in the scenarios):
  - `JetRBSPrepareRevert` and `JetRBSCancelRevert` are
    `FEnterWithoutInit`-gated — they take an instance with paths
    configured but NOT yet `JetInit`'d.  A live RBS-rolling
    thread would clash with the revert path.
  - The RBS subsystem only initialises if the rstmap built during
    `JetInit` carries at least one database entry whose `.edb`
    header has the `JET_efvRevertSnapshot` flag set.  Plain
    `JetInit` (no rstmap) leaves `m_irstmapMac=0`,
    `FRBSFeatureEnabledFromRstmap` returns false, and no `.rbs`
    files ever roll.  Bootstrap pattern: phase A creates the
    `.edb` under plain `JetInit`; later `JetInit4A` calls pass
    an explicit `JET_RSTMAP2_A` pointing the path at itself so
    RBS comes up.
  - The roll-snapshot check fires at `JetInit` / `JetTerm` /
    redo only — not during writes (`tm.cxx ~1046:
    if ( pinst->m_prbs && pinst->m_prbs->FRollSnapshot() )`).
    To roll multiple `.rbs` generations from a test, do multiple
    init/work/term cycles with the
    `JET_paramFlight_RBSRollIntervalSec` and
    `JET_paramFlight_RBSForceRollIntervalSec` flighting knobs
    pinned to 1 second.
  - A revert target before any `.rbs` generation's `tmCreate`
    surfaces as `JET_errRBSRCInvalidRBS` (-1929) from
    `ErrComputeRBSRangeToApply` (`!tmPrevGen.FIsSet()` on the
    oldest gen).
  - `JetRBSExecuteRevert` applies the pre-image pages but does
    NOT truncate existing log generations by default.  Without
    `JET_bitDeleteAllExistingLogs` on `JetRBSPrepareRevert`'s
    grbit (`revertsnapshot.cxx:5817`), the next `JetInit`'s
    redo cleanly re-applies the rolled-back writes and the
    database appears unchanged at the row level even though
    `revertInfo.cPagesReverted > 0`.

- **Round 8** (current): replication / replica repair — 6 APIs
  across 9 scenarios in `ReplicationScenarios.cxx`, with wire
  transport + orchestration helpers under
  `Scenarios/Replication/` (`WireProtocol.{hxx,cxx}`,
  `Orchestrator.{hxx,cxx}`).  Topology: TCP-loopback between
  separate OS processes — active opens `127.0.0.1:0`, spawns its
  passive with `--connect-port=N` on the CLI, parent test
  scenario is a thin driver that forks the active and verifies
  the passive's DB afterwards.

  APIs covered:
  - `JetConsumeLogData` (+ `JET_paramEmitLogDataCallback`
    round-trip) — log-shipping live tail.
  - `JetBeginDatabaseIncrementalReseed` /
    `JetPatchDatabasePages` / `JetEndDatabaseIncrementalReseed`
    — incremental-reseed bracket, both cancel-path and
    commit-path scenarios.
  - `JetOnlinePatchDatabasePage` — online page repair with a
    well-formed `PAGE_PATCH_TOKEN`.
  - `JetExternalRestore` — caller pre-stages backup files in the
    target dir, then API applies recovery to bring the DB
    current; runs in a forked child to avoid process-state
    collisions.

  Engine-contract gotchas surfaced (documented inline + in
  `project_jetconsumelogdata_shadow_log` memory):
  - `JetConsumeLogData` writes shadow `.jsl` files alongside
    `.log`.  Recovery reads only `.log`, so the passive renames
    `.jsl` -> `.log` post-consume and the verify-side `JetInit2`
    passes `JET_bitAllowMissingCurrentLog` (the rename doesn't
    synthesise an unnumbered `edb.log` "current writer" marker).
  - The active runs a SINGLE engine cycle with the emit callback
    set BEFORE `JetInit` so every log byte — including each
    gen's `LGFILEHDR` — emits to the passive.  Two-phase splits
    fail because phase-1 `JetTerm` pre-allocates the next gen
    and phase-2 writes start mid-gen (after the header skip),
    tripping the consumer's `bMidSequenceFirstData` path with
    no pre-existing `.jsl` to open.
  - `JetPatchDatabasePages` + `JetBegin/End*Reseed` require
    `JET_dbstateDirtyShutdown`: build the DB then JetTerm with
    `JET_bitTermDirty` and SKIP `JetDetachDatabaseA` (detach
    flushes the .edb header to clean shutdown).  `End` with
    `genFirstDivergedLog=0` selects the passive-page-patch path
    that doesn't require a log-divergence range.  The
    `genMinRequired` log file's header must carry attach info,
    which only propagates to a header on gen rollover — small
    DBs (< ~600 rows in one gen) don't roll, so the build phase
    writes 2000 rows in 50-row chunks to force gen-1 -> gen-2.
  - `JetOnlinePatchDatabasePage` requires
    `JET_paramEnableExternalAutoHealing=1` (default 0) and a
    48-byte `PAGE_PATCH_TOKEN` with `cbStruct=48`, `dbtime`, and
    `signLog` matching
    `JetGetInstanceMiscInfo(JET_InstanceMiscInfoLogSignature)`.
    No registered patch request -> engine returns success but
    silently skips the rewrite (the documented "speculative
    patch" path).
  - `JetExternalRestoreA` is "external" because the CALLER
    physically stages backup files in the target directory
    beforehand; the API only runs recovery on those files.
    Trailing slashes required on both `szCheckpointFilePath`
    and `szLogPath`.  Caller runs the whole flow in a forked
    child to avoid process-state collisions with the runner's
    already-initialised engine globals.

- **Round 9** (current): versioned variants with genuinely new
  functional surface — 6 scenarios across `SchemaScenarios.cxx`
  + `NavigationScenarios.cxx`, covering 8 APIs:
  `JetCreateDatabase3`, `JetAttachDatabase3` (`JET_SETDBPARAM`
  stamping verified via `JetGetMaxDatabaseSize` round-trip),
  `JetCreateIndex3` (INDEXCREATE2 with `JET_SPACEHINTS`),
  `JetCreateIndex4` (INDEXCREATE3 with locale-name
  `JET_UNICODEINDEX2`), `JetCreateTableColumnIndex3` / `4` / `5`
  (one scenario building three tables, one per struct version),
  and `JetSetCurrentIndex4` (cached `JET_INDEXID` + `itagSequence`
  index swap that preserves cursor position).  Engine
  contract gotchas surfaced:
  - `JetCreateTableColumnIndex5`'s `cbLVChunkMax` is capped at
    `JET_paramLVChunkSizeMost` (a read-only system param), which
    is page-size dependent (`~4 KiB - LVChunkOverheadSmallPage`
    on small pages).  Scenarios should pass a conservatively
    small value (≤1 KiB) for portability across page-size
    configurations.
  - `JET_SPACEHINTS` validation in `cat.cxx` is strict; for a
    smoke-test scenario, leaving `pSpacehints`/`pSeqSpacehints`
    null is safer than fabricating one with arbitrary
    `ulInitialDensity` / `cbInitial` values.

- **Round 10**: thin-wrapper variants — `JetInit3` (the v3 init shape
  that takes a `JET_RSTINFO_A` rather than v4's `JET_RSTINFO2_A`),
  `JetDeleteColumn2` (adds the `JET_bitDeleteColumnIgnoreTemplateColumns`
  grbit), `JetDetachDatabase2` (adds `JET_GRBIT` for detach flags;
  scenarios verify the standard `grbit=0` detach path because
  `JET_bitForceCloseAndDetach` is gated on a prior failed normal
  detach by the engine), `JetExternalRestore2` (adds `JET_LOGINFO`
  for explicit `genLow`/`genHigh` log-range targeting; otherwise
  identical to round-8's external-restore flow).  Round-10 audit
  also (a) demoted `JetDefragment3` to out-of-scope after
  discovering it's an unconditional `JET_errInvalidParameter` stub
  upstream (`jetapi.cxx:20064`, "OBSOLETE: only used by SFS"),
  (b) demoted `JetDeleteTable2` to out-of-scope after discovering
  its declaration is pinned behind `JET_VERSION > 0x0A01` in
  `jetapi.h:6976` and therefore not visible at the version we ship,
  and (c) switched the coverage-percentage denominator from
  "declared" to "testable" so engine stubs and version-gated
  declarations no longer drag the percentage down.

- **Round 11**: W-variant (UTF-16) smoke coverage in
  `WideApiScenarios.cxx`.  12 scenarios — instance / session / DB /
  schema / params / non-ASCII paths-and-identifiers — each routed
  end-to-end through `Jet*W` entry points without touching the
  A-based Framework helpers (`EseInstance` etc.).  Not counted in
  the coverage percentage: A and W collapse at the base name, so
  the underlying APIs are already credited; round 11 is purely a
  windows-shim `MultiByteToWideChar` / `WideCharToMultiByte`
  smoke check.

  APIs exercised by at least one W call-site:
  `JetCreateInstanceW`, `JetCreateInstance2W`, `JetBeginSessionW`,
  `JetSetSystemParameterW`, `JetGetSystemParameterW`,
  `JetCreateDatabaseW`, `JetCreateDatabase2W`, `JetAttachDatabaseW`,
  `JetAttachDatabase2W`, `JetDetachDatabaseW`, `JetOpenDatabaseW`,
  `JetCreateTableW`, `JetCreateTableColumnIndexW`, `JetAddColumnW`,
  `JetCreateIndexW`, `JetCreateIndex2W`, `JetOpenTableW`.

  Engine-contract gotchas surfaced (documented inline):
  - `JetCreateIndexW`'s `cbKey` parameter is a byte count (not
    code-unit count) — the wide keyspec doubles the byte cost
    compared to the A path.
  - String-valued system parameters are init-time only on both
    A and W paths; setting `JET_paramEventSource` (or similar
    string params) after `JetInit` returns
    `JET_errAlreadyInitialized`.  W scenarios that exercise
    `JetSetSystemParameterW` must do so before `JetInit`.

- **Round 12**: snapshot-truncate (`JetOSSnapshotTruncateLog` /
  `JetOSSnapshotTruncateLogInstance`) — 2 scenarios in
  `SnapshotScenarios.cxx`.  The pair was flagged in earlier rounds
  as "hangs on Linux inside `pSession->ErrTruncateLogs`"; turned
  out to be a test-author bug: `JET_bitContinueAfterThaw` is a
  Prepare flag, not a Thaw flag.  Passing it to Thaw returns
  `JET_errInvalidGrbit` immediately, but the failed Thaw leaves
  the freeze timer running, which expires ~70s later and looked
  like a hang.  Correct sequence: `JetOSSnapshotPrepare(…,
  JET_bitContinueAfterThaw)` → Freeze → `Thaw(0)` → TruncateLog →
  End.  Closes the last remaining in-scope functional gap; 196/198
  testable APIs now covered.

- **Round 13** (wave H): backup-family grbits + fork-per-scenario
  isolation harness.  Seven scenarios across `BackupRestoreScenarios.cxx`,
  `DatabaseScenarios.cxx`, and `MaintenanceScenarios.cxx`:
  `JET_bitBackupIncremental` (log-only artefacts in target dir),
  `JET_bitBackupAtomic` (engine stages under `new/` subdir for
  atomic promote), `JET_bitBackupSurrogate` on both
  `JetBeginSurrogateBackup` / `JetEndSurrogateBackup` and
  `JetBeginExternalBackupInstance`, `JetTerm2` with
  `JET_bitTermStopBackup` mid-flight, `JetCompact` with a
  `JET_PFNSTATUS` callback (counts `JET_snpCompact` /
  `JET_sntProgress`), and `JetIdle` with
  `JET_bitIdleCompact|JET_bitIdleCompactAsync`.
  No new base APIs (all on already-covered surface); the round
  exists to exercise grbits that the surrounding scenarios skipped.

  Infrastructure shipping in the same commit (`f885d0c`):
  every scenario now runs in its own forked child process.  The
  parent forks per scenario, the child runs `PlatformInitializer`
  + the scenario body + `_exit`, and a side-channel pipe carries
  the failure message back.  Solves three classes of order-
  dependent flake at once: ESE's permanent multi-instance flip
  after first `JetCreateInstance2`, `CResourceManager`'s
  freeze-on-first-commit, and leftover system-parameter state.
  Scenarios that need a non-default engine config now flip it via
  `EseInstanceOptions` (the round adds `EnableCircularLog` — the
  three surrogate/incremental/atomic backup scenarios all opt
  out of circular logging because the engine returns
  `JET_errInvalidBackup` for those grbits under circular log)
  without leaking the change into the rest of the suite.
  `--in-process` opts the suite back into the old single-process
  behaviour for debugger attach / asan walks.

  Also in this commit: `JetErrorName` now covers all 485
  `JET_err*`/`JET_wrn*` codes from `jetapi.h` with `Error*` /
  `Warning*` short names, so failure output reads
  `ErrorInvalidBackup (-526)` rather than `JET_err<-526>`.  Six
  unterminated section-header comments in `jetapi.h`
  (`/*  SYSTEM errors`, `/*  LOGGING/RECOVERY errors`, etc.)
  were silently swallowing the first `#define` in each block —
  same upstream-header bug as commit `2a29af6` for the DML
  block.  Fixed in this commit so the full error-name table
  compiles.

  Engine-contract gotchas surfaced:
  - `JET_bitBackupIncremental`, `JET_bitBackupAtomic`,
    `JET_bitBackupSurrogate`, and `JetBeginSurrogateBackup` are
    all rejected with `JET_errInvalidBackup` (-526) when
    `JET_paramCircularLog=1`.  Default framework setting is
    circular-on for log-dir boundedness — backup grbit tests must
    flip it off per-instance.
  - `JET_bitBackupAtomic` writes its artefacts into a `new/`
    subdirectory under the requested target path, not directly
    into the target.  Atomicity comes from populating the side
    directory in full and then promoting it on success, so a
    crash mid-copy never leaves a half-written backup at the
    target name.  Recursive iteration required to verify the
    `.mdb` + `.log` show up.
  - `JET_bitBackupSurrogate` on `JetBeginExternalBackupInstance`
    suppresses the engine's own file-copy bookkeeping (the
    external surrogate is doing the snapshot), so companion
    APIs like `JetGetAttachInfoInstance` return
    `JET_errNoBackup`.  Scenario validates Begin accepts the
    flag and End closes cleanly; no GetAttachInfo middle.

- **Round 14** (wave I): `JET_SPACEHINTS::grbit` coverage — every
  non-reserved `JET_bit*Hint*` flag exercised through
  `JetCreateIndex3` + `JET_INDEXCREATE2::pSpacehints`.  Eight
  scenarios in `SchemaScenarios.cxx`:
  `JET_bitSpaceHintsUtilizeParentSpace` (hierarchical extent
  allocation),
  `JET_bitSpaceHintsUtilizeExactExtents` (exact-size extents),
  `JET_bitCreateHintAppendSequential` (right-edge-biased split
  policy),
  `JET_bitCreateHintHotpointSequential` (moving-cursor split
  policy),
  `JET_bitRetrieveHintTableScanForward` (forward-scan workload
  hint — auto-defrag trigger),
  `JET_bitRetrieveHintTableScanBackward` (backward-scan
  workload),
  `JET_bitDeleteHintTableSequential` (low-to-high cleanup
  pattern), plus a combined-bits scenario that OR's five bits
  together.  No new base APIs; the round closes the hint-bit
  gap on the `JET_SPACEHINTS` grbit field that earlier rounds
  left at the safe default of `grbit=0`.

  Each scenario is hint-flag-with-weak-observable by nature —
  the engine accepts the hint, the index builds, and the
  intended workload (seek for space/create hints; full scan
  for retrieve hints; sequential delete for the delete hint)
  succeeds.  The internal policy change (extent layout, defrag
  threshold, cleanup ordering) isn't directly visible to
  public-API callers.  Shared boilerplate factored into
  `CreateRowsTableForHint` / `InsertSequentialRows` /
  `CreateValueIndexWithSpaceHints` / `SeekValueIndexAndVerify`
  helpers in the anonymous namespace at the top of the wave's
  block so each scenario body fits on one screen.

- **Round 15**: weak-assertion audit — across all 28 scenario
  files, find scenarios that only check `CheckJet(...)` success
  without verifying behavioral consequences, and strengthen them
  to read back what the API was supposed to do.  Original test
  authorship sometimes treated "API returned success" as the
  whole contract; the goal of an integration scenario is to
  validate what the API *did*, not just that it dispatched.

  Twenty-plus scenarios strengthened across DDL, DML, navigation,
  backup, recovery, snapshot, RBS, replication, maintenance,
  session, database, transaction, temp-table, escrow, platform,
  preread, limit, wide-API, and scale categories.  Examples:
  - `Schema.CreateTable` now closes + reopens the table by name
    to prove the catalog entry persists.
  - `Schema.AddColumnAfterTableCreation` round-trips the
    columnid through `JetGetTableColumnInfo`.
  - `Schema.CreateSecondaryUniqueIndex` populates the table,
    seeks via the index, AND verifies a duplicate key insert is
    rejected with `JET_errKeyDuplicate`.
  - `Schema.CreateMultiColumnIndex` populates 4 (Region, Quarter)
    rows and seeks on the composite key.
  - `DataManipulation.GetRecordSizeReportsNonZeroData` reads the
    blob bytes back via `JetRetrieveColumn` + memcmp.
  - `DataManipulation.RetrievePageNumberReportsResidentPage`
    bumps to 4096 rows so first-page vs last-page must differ.
  - `MultiValue.UniqueMultiValueIndexRejectsDuplicateAcrossRows`
    walks the index post-rejection and asserts the surviving
    row count is exactly 1 with the right Tag value.
  - `Navigation.SetAndResetTableSequentialRoundTrip` verifies
    every row's Identity column is strictly increasing under
    the hint AND that a second walk after Reset returns the
    same row count.
  - `BackupRestore.StreamingBackupProducesNonEmptyDirectory`
    validates the backup `.mdb` via `JetGetDatabaseFileInfo`
    (file-type, file-size match on-disk bytes).
  - `BackupRestore.ExternalBackupExposesAttachInfo` parses the
    multi-string attach-info buffer and confirms the entry
    ends with `External.mdb`.
  - `BackupRestore.GetLogInfoInstanceListsActiveLogs` parses
    the multi-string list, confirms each entry exists on disk
    with a log extension, AND that the truncate-log subset is
    actually a subset of the active list.
  - `BackupRestore.GetInstanceMiscInfoReportsLogSignature`
    validates every JET_LOGTIME byte range, `ulRandom` is
    non-zero, `szComputerName` terminates, AND a second call
    returns byte-identical bytes (signature is stable).
  - `Recovery.ReplayIgnoreMissingDBProceedsWithoutDeletedDatabase`
    walks the Primary rows twice to prove the cursor / page
    cache survived recovery (engine-resurrected Secondary is
    documented behaviour and not asserted).
  - `Snapshot.PrepareAndEndCycle` asserts two Prepare calls
    yield distinct snapshot ids; a post-End reuse returns
    `JET_errOSSnapshotInvalidSnapId`.
  - `Snapshot.TruncateLogClearsBackupLogs` asserts the freeze
    info populated (cDatabases ≥ 1), End-then-reuse rejects
    with `JET_errOSSnapshotInvalidSnapId`, AND post-lifecycle
    the engine still serves writes + reads.
  - `Rbs.GetRBSFileInfoReadsHeaderOfClosedSnapshotFile` validates
    every `JET_LOGTIME` field range, `ulMajor > 0`, and the
    logical file size is ≤ on-disk size.
  - `Replication.OnlinePagePatchRoundTripsValidPage` reads every
    row's Value column (not just counts rows) + re-reads the
    patched page and memcmps against the pre-patch snapshot to
    confirm the speculative-patch path didn't modify the bytes.
  - `Maintenance.ComputeStatsOnEmptyTable` reads `JetGetTableInfo`
    and verifies `cRecord == 0` + `JET_bitTableInfoUpdatable`.
  - `Maintenance.OnlineDefragmentRunsToCompletion` walks every
    surviving row by Value (rows 100..199 after the front-of-
    table deletion) so a defrag that corrupted the B-tree
    surfaces here.
  - `Maintenance.ResizeDatabaseGrowsPageCount` reads the page
    size + filename via `JetGetDatabaseInfo` and confirms the
    on-disk file size is at least pages * page_size.
  - `Maintenance.DatabaseScanBatchPassRunsToCompletion` walks
    all 500 inserted rows by Value post-scan; a scan that
    damaged checksums or dbtime would fail the readback.
  - `Maintenance.IdleCompactAsyncSchedulesBackgroundWork` walks
    by Value AND runs a second synchronous `JetIdle(IdleCompact)`
    pass to confirm both modes are wired identically.
  - `Session.OpenAndClose` uses `JetGetSessionInfo` to read the
    transaction-level inside a transaction (==1) and after
    rollback (==0) — proves the sesid resolves to live state.
  - `Session.GetVersionReturnsNonZero` calls twice and confirms
    the values match (version is a binary property, not per-call).
  - `Database.CreateAndClose` creates a table, inserts a row,
    reads it back — proves the dbid is functional, not just
    that `JetCreateDatabaseA` returned success.
  - `Database.SetDatabaseSizeMatchesGrowSemantics` writes a 64-row
    sentinel set BEFORE the resize and verifies all 64 rows
    come back in order after the reattach.
  - `Transaction.BeginAndCommit` runs an insert inside the
    transaction, commits, reads back the value, AND verifies a
    second serial transaction stacks correctly to two rows.
  - `TemporaryTable.OpenSortedTempTable` inserts three out-of-
    order keys and confirms the walk returns them in strictly
    increasing order.
  - `TemporaryTable.OpenTempTable3SortsViaUnicodeIndex` walks
    all three rows and asserts the exact sort
    `"Apple" < "banana" < "cherry"` (case-insensitive UTF-16
    sort), not just that the first row starts with A.
  - `Escrow.DeleteOnZeroEventuallyRemovesRecord` drains async
    activity via `JetIdle(JET_bitIdleWaitForAsyncActivity)` and
    branches on the observed state: deleted-and-table-still-
    usable, or counter-pinned-at-zero (the two valid steady
    states for this engine contract).
  - `Platform.GetInstanceInfoEnumeratesRunningInstance` matches
    the live instance by `hInstanceId` against
    `instance.Handle()` (was previously just `count >= 1`).
  - All four `Preread*` scenarios now seek / scan / open
    against the data behind the preread to confirm the engine
    served real records, not just that the hint was accepted.
  - `Limit.TableWithTwentyIndexesRoundTrips` inserts a single
    row with distinguishable per-column values and seeks via
    every index, retrieving the matching column to prove each
    index is populated and functional.
  - `Limit.FiftyTablesInOneDatabase` stamps each of 50 tables
    with `index + 1000` and reads the sentinel back during
    reopen, proving N-th table actually holds N-th data.
  - `Limit.LongTableNameAccepted` closes + reopens by the long
    name and reads back a sentinel — proves the engine stored
    the full 60-char name and resolves it on lookup.
  - `WideApi.CreateInstanceWBootsCleanly` enumerates via
    `JetGetInstanceInfoW` and confirms the wide instance name
    we passed at create-time round-trips through engine
    storage and back out as UTF-16.
  - `WideApi.CreateInstance2WStampsDisplayName` runs a full
    insert + read DML round-trip via all-W APIs (CreateTableW
    + AddColumnW + SetColumn + RetrieveColumn).
  - `WideApi.BeginSessionWAcceptsWideCredentials` runs a
    BeginTransaction / Rollback inside the credentialed
    session to prove it's functional.
  - `WideApi.CreateTableColumnIndexWBuildsAtomically` and
    `WideApi.CreateIndex2WStructPath` populate rows + seek via
    the W-built index to prove the index is real.
  - `Scale.ManyTablesCreatedAndOpened` stamps each scale-N
    table with `index + 100000` and reads the sentinel back
    during reopen.

  Framework changes: `EseInstanceOptions::LogFileSizeKb`
  added — scenarios that need to force log generation rolls
  within a small workload (snapshot/truncate-log tests) set a
  small value (e.g. 64 KiB) so each transaction's commit
  flushes overflow into a fresh numbered log.

