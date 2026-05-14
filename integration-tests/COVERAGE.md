# JET API coverage inventory

Source: `jetapi.h` (the standalone repo-root header), tracked against the
`integration-tests` corpus.  Coverage is measured at the **base** API
level (A/W variants collapsed): if `JetCreateInstanceA` is called
anywhere in the tests, the base name `JetCreateInstance` is treated as
covered.  Version-suffixed siblings (e.g. `JetCreateIndex2`,
`JetCreateIndex3`, `JetCreateIndex4`) are tracked separately from their
unversioned base.

**Totals:** 212 base APIs declared, 120 covered (57%), 92 untested.

## Tested (120)

Core surface for every scenario the engine actually runs.  Includes
DDL (table/column/index create/delete/rename, **JetDeleteTable**,
**JetSetColumnDefaultValue**), DML
(insert/update/delete/retrieve, **JetEnumerateColumns**,
**JetRetrieveTaggedColumnList**), navigation
(move/seek/setIndex/setRange, **JetIntersectIndexes**,
**JetSetCursorFilter**, **JetSetTableSequential** /
**JetResetTableSequential**), transactions
(begin/commit/rollback, **JetPrepareToCommitTransaction**),
backup/restore (incl. external + restore, **JetStopBackupInstance**),
snapshot lifecycle, params, init/term, attach/detach/open/close,
JetCompact / JetDefragment, JetEscrowUpdate, JetComputeStats,
JetGetBookmark / JetGotoBookmark, JetFreeBuffer.

Diagnostics: **JetGetSessionInfo**, **JetGetCursorInfo**,
**JetGetVersion**, **JetGetThreadStats**.

Cursor handle management: **JetDupSession**, **JetDupCursor**.

Cursor / position queries: **JetGetCurrentIndex**,
**JetGetRecordPosition**, **JetGotoPosition**,
**JetGetSecondaryIndexBookmark**, **JetGotoSecondaryIndexBookmark**.

Database lifecycle: **JetGrowDatabase**, **JetSetDatabaseSize**,
**JetSetMaxDatabaseSize**, **JetGetMaxDatabaseSize**,
**JetResizeDatabase**.

Engine hooks: **JetRegisterCallback**, **JetUnregisterCallback**,
**JetIdle** (`JET_bitIdleWaitForAsyncActivity` /
`JET_bitIdleAvailBuffersStatus`).

Info surface: **JetGetDatabaseInfo**, **JetGetObjectInfo**,
**JetGetIndexInfo**, **JetGetInstanceInfo**,
**JetGetInstanceMiscInfo**, **JetGetDatabaseFileInfo**,
**JetGetLogFileInfo**, **JetGetLogInfoInstance**,
**JetGetTruncateLogInfoInstance**, **JetGetAttachInfo** (global),
**JetGetPageInfo**, **JetGetDatabasePages**, **JetGetSystemParameter**.

Record-level: **JetRetrieveKey**, **JetGetLock**, **JetGetRecordSize**,
**JetIndexRecordCount**.

Session / cursor local storage: **JetSetSessionContext**,
**JetResetSessionContext**, **JetSetLS**, **JetGetLS**,
**JetSetSessionParameter**, **JetGetSessionParameter**.

Pre-read surface: **JetPrereadKeys**, **JetPrereadIndexRange**,
**JetPrereadTables**.

Counters: **JetGetCounter**, **JetResetCounter** (FNA-tolerant — the
Linux build dispatches the calls but the underlying counters aren't
ported yet; both the call shape and the engine's FNA response are
exercised).

Logs / backup: **JetTruncateLogInstance** (covers the call shape +
out-of-sequence error path).

Instance lifecycle: **JetCreateInstance** (unversioned) in addition
to `JetCreateInstance2`.

## Genuine functional gaps (no version covered)

The engine ships these and no test exercises any version.  Roughly
ordered by user-visible value.

### DML
- `JetRetrieveColumnByReference`, `JetRetrieveColumnFromRecordStream`,
  `JetPrereadColumnsByReference`, `JetStreamRecords` — column-stream
  surface
- `JetGetRecordSize2` / `JetGetRecordSize3` — newer revs (base `JetGetRecordSize` covered)

### Pre-read surface
- `JetPrereadIndexRanges` — multi-range form (singular `JetPrereadIndexRange` covered)

### Database lifecycle
- `JetUpgradeDatabase`, `JetConvertDDL`
- `JetDatabaseScan` — online corruption/integrity scan

### Open-file surface
- `JetOpenFile`, `JetOpenFileInstance`, `JetOpenFileSectionInstance`,
  `JetReadFile`, `JetReadFileInstance`, `JetCloseFile`,
  `JetCloseFileInstance` — raw-file (.edb/.log) access for backup
  agents

### Logs / replay
- `JetTruncateLog`, `JetRemoveLogfile`,
  `JetGetLogInfo`, `JetGetLogInfoInstance2`
- `JetConsumeLogData`, `JetExternalRestore`, `JetExternalRestore2`,
  `JetBeginExternalBackup`, `JetEndExternalBackup` (we have the
  *Instance* variants but not the global ones)
- `JetStopBackup` (`JetStopBackupInstance` covered)
- `JetBeginDatabaseIncrementalReseed`, `JetEndDatabaseIncrementalReseed`
- `JetBeginSurrogateBackup`, `JetEndSurrogateBackup`

### Page / index inspection
- `JetGetPageInfo2` (base `JetGetPageInfo` covered)
- `JetIndexRecordCount2` — newer rev (base covered)
- `JetOnlinePatchDatabasePage`, `JetPatchDatabasePages`

### Snapshot extensions
- `JetOSSnapshotAbort`, `JetOSSnapshotGetFreezeInfo`,
  `JetOSSnapshotPrepareInstance`, `JetOSSnapshotTruncateLog`,
  `JetOSSnapshotTruncateLogInstance`
- `JetSnapshotStart`, `JetSnapshotStop`

### Revertable-Backup-Set (RBS)
- `JetRBSPrepareRevert`, `JetRBSExecuteRevert`, `JetRBSCancelRevert`,
  `JetGetRBSFileInfo`

### Service / housekeeping
- `JetStopService`, `JetStopServiceInstance`, `JetStopServiceInstance2`

### Multi-instance / params
- `JetEnableMultiInstance`
- `JetGetResourceParam`, `JetSetResourceParam`
- `JetConfigureProcessForCrashDump`
- `JetGetErrorInfo` — structured error metadata
- `JetCreateEncryptionKey` is now exercised by `Encryption.*` scenarios

### Tools / hooks
- `JetTracing`, `JetTestHook`, `JetDBUtilities`

## Version-variant gaps (newer surface, base form covered)

These are "v2/v3/v4 wrappers add new params on top of a tested base."
Low-priority; the underlying functionality is exercised.

- `JetInit2` / `JetInit3` / `JetInit4` (`JetInit` covered)
- `JetUpdate2` (`JetUpdate` covered)
- `JetAttachDatabase2` / `JetAttachDatabase3`
- `JetCreateDatabase2` / `JetCreateDatabase3`
- `JetCreateIndex2` / `JetCreateIndex3` / `JetCreateIndex4`
- `JetCreateTableColumnIndex2` / `3` / `4` / `5`
- `JetDefragment2` / `JetDefragment3`
- `JetDeleteColumn2`
- `JetDeleteTable2` (`JetDeleteTable` covered)
- `JetDetachDatabase2`
- `JetOpenTemporaryTable` / `JetOpenTemporaryTable2` /
  `JetOpenTempTable2` / `JetOpenTempTable3` (`JetOpenTempTable` covered)
- `JetSetCurrentIndex2` / `JetSetCurrentIndex3` / `JetSetCurrentIndex4`
- `JetBeginTransaction3` (1+2 covered)
- `JetCommitTransaction2`
- `JetEndExternalBackupInstance2`
- `JetRestore` / `JetRestore2` (the *Instance* variant covered)
- `JetBackup` (the *Instance* variant covered)
- `JetTerm` / `JetTerm2` (both covered, but `Term2` only by framework wrapper)
- `JetExternalRestore` / `JetExternalRestore2`

## Suggested next priorities (high-value, low-cost scenarios)

**Rounds 1 + 2 + 3 landed.**  Round 3 added `JetGetCurrentIndex`,
`JetSetTableSequential` / `JetResetTableSequential`,
`JetSetCursorFilter`, `JetGetRecordPosition` / `JetGotoPosition`,
`JetGetSecondaryIndexBookmark` / `JetGotoSecondaryIndexBookmark`,
`JetSetSessionParameter` / `JetGetSessionParameter`,
`JetGetSystemParameter`, `JetGetCounter` / `JetResetCounter`,
`JetSetColumnDefaultValue`, `JetResizeDatabase`,
`JetGetAttachInfo` (global), `JetGetLogInfoInstance`,
`JetGetTruncateLogInfoInstance`, `JetTruncateLogInstance`,
`JetStopBackupInstance`, `JetGetInstanceMiscInfo`,
`JetGetLogFileInfo`, `JetGetDatabaseFileInfo`, `JetGetPageInfo`,
`JetGetDatabasePages`, `JetRetrieveTaggedColumnList`,
`JetPrereadKeys`, `JetPrereadIndexRange`, `JetPrereadTables`,
`JetPrepareToCommitTransaction`, `JetCreateInstance` (unversioned) —
31 base APIs in one push, advancing the meter from 42% to 57%.

Round 4 candidates from the remaining gap list:

1. **`JetEnableMultiInstance`** — requires a forked child for a pristine
   engine (single-instance mode locks in at first JetSetSystemParameter
   in the parent).  CrashHelper already has the fork machinery.
2. **`JetGetErrorInfo`** — structured `JET_ERRINFOBASIC_W` for a
   captured engine error; pairs with the ErrorScenarios matrix.
3. **`JetPrereadIndexRanges`** (multi-range) — extend
   `Preread.PrereadIndexRangeAcceptsBoundedRange` to two ranges.
4. **`JetDatabaseScan`** — online integrity scan; a "scan empty DB
   succeeds" scenario covers the API entry.
5. **`JetGetResourceParam` / `JetSetResourceParam`** — pre-init
   resource-pool tuning; need a fresh instance.
6. **`JetGetPageInfo2`** — same shape as `JetGetPageInfo`, with the
   extended checksum array.
7. **`JetOSSnapshotPrepareInstance`** — instance-scoped snapshot
   alongside the existing global prepare/freeze/thaw.
8. **`JetOSSnapshotAbort`** — abort path between `Freeze` and `Thaw`.

## What's NOT in scope here

- **JetDBUtilities / JetTestHook / JetTracing** — engineering surface,
  not user-facing.  Coverage gap by design.
- **External backup (`JetExternalRestore`/`*RestoreInstance`)** — the
  *Instance* variant is already used in `BackupRestore.*`; the global
  forms are deprecated wrappers.
- **Versioned siblings of covered bases** — listed for completeness
  but not real coverage gaps; the underlying capability is exercised.
