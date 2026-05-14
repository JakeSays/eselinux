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

**Totals:** 212 base APIs declared, 177 covered (83%), 35 untested.

## Tested (177)

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
**JetOSSnapshotPrepareInstance**, **JetOSSnapshotGetFreezeInfo**.

Instance lifecycle: **JetCreateInstance** (unversioned) in addition
to `JetCreateInstance2`, **JetInit2**, **JetEnableMultiInstance**
(forked-child scenario for pristine engine).

Service control: **JetStopService**, **JetStopServiceInstance**,
**JetStopServiceInstance2** (with `JET_bitStopServiceBackgroundUserTasks`
+ `JET_bitStopServiceResume`).

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
**JetCreateIndex2** (JET_INDEXCREATE struct),
**JetCreateDatabase2** / **JetAttachDatabase2** (size-cap),
**JetCreateTableColumnIndex2** (callback hook),
**JetOpenTempTable3** (JET_UNICODEINDEX),
**JetDefragment2** (callback),
**JetGetPageInfo2**, **JetGetRecordSize2**, **JetGetRecordSize3**,
**JetIndexRecordCount2**.

## Genuine functional gaps (no version covered)

The engine ships these and no test exercises any version.  Roughly
ordered by user-visible value.

### Logs / replay
- `JetExternalRestore2` (round 8 covered the v1 form; v2 adds a
  `JET_LOGINFO` argument and is a thin wrapper over the same
  ErrIsamExternalRestore path)

### Snapshot extensions
- `JetOSSnapshotTruncateLog`, `JetOSSnapshotTruncateLogInstance` —
  blocked on engine investigation (hang inside
  `pSession->ErrTruncateLogs` on this Linux build)

## Version-variant gaps (newer surface, base form covered)

These are "v2/v3/v4 wrappers add new params on top of a tested base"
that don't add observable functional surface beyond their v1/v2
sibling.  Low-priority; the underlying capability is exercised.

- `JetInit3` / `JetInit4` (`JetInit`, `JetInit2` covered)
- `JetAttachDatabase3` (`JetAttachDatabase` / `JetAttachDatabase2`
  covered)
- `JetCreateDatabase3` (`JetCreateDatabase` / `JetCreateDatabase2`
  covered)
- `JetCreateIndex3` / `JetCreateIndex4` (`JetCreateIndex2` covered)
- `JetCreateTableColumnIndex3` / `4` / `5` (`JetCreateTableColumnIndex`
  / `2` covered)
- `JetDefragment3` (`JetDefragment` / `JetDefragment2` covered)
- `JetDeleteColumn2`
- `JetDeleteTable2` (`JetDeleteTable` covered)
- `JetDetachDatabase2`
- `JetOpenTemporaryTable` / `JetOpenTemporaryTable2` /
  `JetOpenTempTable2` (`JetOpenTempTable` / `JetOpenTempTable3`
  covered)
- `JetSetCurrentIndex3` / `JetSetCurrentIndex4`
  (`JetSetCurrentIndex` / `JetSetCurrentIndex2` covered)
- `JetRestore` / `JetRestore2` (the *Instance* variant covered)
- `JetBackup` (the *Instance* variant covered)

## Out of scope (engineering surface)

These ship in `jetapi.h` but are intentionally excluded from the
coverage target:

- **JetDBUtilities / JetTestHook / JetTracing** — engineering surface,
  not user-facing.
- **External backup (`JetExternalRestore`/`*RestoreInstance`)** — the
  *Instance* variant is already used in `BackupRestore.*`; the global
  forms are deprecated wrappers.
- **Versioned siblings of covered bases** — listed for completeness
  but not real coverage gaps; the underlying capability is exercised.
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

