# JET API coverage inventory

Source: `jetapi.h` (the standalone repo-root header), tracked against the
`integration-tests` corpus.  Coverage is measured at the **base** API
level (A/W variants collapsed): if `JetCreateInstanceA` is called
anywhere in the tests, the base name `JetCreateInstance` is treated as
covered.  Version-suffixed siblings (e.g. `JetCreateIndex2`,
`JetCreateIndex3`, `JetCreateIndex4`) are tracked separately from their
unversioned base.

**Totals:** 212 base APIs declared, 89 covered (42%), 123 untested.

## Tested (89)

Core surface for every scenario the engine actually runs.  Includes
DDL (table/column/index create/delete/rename, **JetDeleteTable**), DML
(insert/update/delete/retrieve, **JetEnumerateColumns**), navigation
(move/seek/setIndex/setRange, **JetIntersectIndexes**), transactions
(begin/commit/rollback), backup/restore (incl. external + restore),
snapshot lifecycle, params, init/term, attach/detach/open/close,
JetCompact / JetDefragment, JetEscrowUpdate, JetComputeStats,
JetGetBookmark / JetGotoBookmark, JetFreeBuffer.

Diagnostics: **JetGetSessionInfo**, **JetGetCursorInfo**,
**JetGetVersion**, **JetGetThreadStats**.

Cursor handle management: **JetDupSession**, **JetDupCursor**.

Database lifecycle: **JetGrowDatabase**, **JetSetDatabaseSize**,
**JetSetMaxDatabaseSize**, **JetGetMaxDatabaseSize**.

Engine hooks: **JetRegisterCallback**, **JetUnregisterCallback**,
**JetIdle** (`JET_bitIdleWaitForAsyncActivity` /
`JET_bitIdleAvailBuffersStatus`).

Info surface: **JetGetDatabaseInfo**, **JetGetObjectInfo**,
**JetGetIndexInfo**, **JetGetInstanceInfo**.

Record-level: **JetRetrieveKey**, **JetGetLock**, **JetGetRecordSize**,
**JetIndexRecordCount**.

Session / cursor local storage: **JetSetSessionContext**,
**JetResetSessionContext**, **JetSetLS**, **JetGetLS**.

## Genuine functional gaps (no version covered)

The engine ships these and no test exercises any version.  Roughly
ordered by user-visible value.

### Cursor / stream
- `JetGetCurrentIndex` — diagnostics
- `JetSetCursorFilter` — server-side row filter
- `JetSetTableSequential` / `JetResetTableSequential` — sequential
  scan hint
- `JetGetRecordPosition`, `JetGotoPosition` — record-position
  navigation (fraction-of-table)
- `JetGetSecondaryIndexBookmark`, `JetGotoSecondaryIndexBookmark` —
  cross-index navigation

### DML
- `JetRetrieveTaggedColumnList` — tag enumeration on tagged columns
- `JetRetrieveColumnByReference`, `JetRetrieveColumnFromRecordStream`,
  `JetPrereadColumnsByReference`, `JetStreamRecords` — column-stream
  surface
- `JetGetRecordSize2` / `JetGetRecordSize3` — newer revs (base `JetGetRecordSize` covered)
- `JetSetColumnDefaultValue` — change a column's default

### Pre-read surface
- `JetPrereadKeys`, `JetPrereadIndexRange`, `JetPrereadIndexRanges`,
  `JetPrereadTables`

### Sessions / context
- `JetSetSessionParameter`, `JetGetSessionParameter`

### Database lifecycle
- `JetResizeDatabase` — DB-size lifecycle (Grow/SetSize covered)
- `JetUpgradeDatabase`, `JetConvertDDL`
- `JetDatabaseScan` — online corruption/integrity scan

### Open-file surface
- `JetOpenFile`, `JetOpenFileInstance`, `JetOpenFileSectionInstance`,
  `JetReadFile`, `JetReadFileInstance`, `JetCloseFile`,
  `JetCloseFileInstance` — raw-file (.edb/.log) access for backup
  agents

### Logs / replay
- `JetTruncateLog`, `JetTruncateLogInstance`, `JetRemoveLogfile`,
  `JetGetLogInfo`, `JetGetLogInfoInstance`, `JetGetLogInfoInstance2`,
  `JetGetLogFileInfo`, `JetGetTruncateLogInfoInstance`
- `JetGetAttachInfo`, `JetGetInstanceInfo`, `JetGetInstanceMiscInfo`
- `JetConsumeLogData`, `JetExternalRestore`, `JetExternalRestore2`,
  `JetBeginExternalBackup`, `JetEndExternalBackup` (we have the
  *Instance* variants but not the global ones)
- `JetStopBackup`, `JetStopBackupInstance`
- `JetBeginDatabaseIncrementalReseed`, `JetEndDatabaseIncrementalReseed`
- `JetBeginSurrogateBackup`, `JetEndSurrogateBackup`

### Page / index inspection
- `JetGetPageInfo`, `JetGetPageInfo2`, `JetGetDatabasePages`
- `JetGetDatabaseFileInfo` — additional info-query surface
  (`JetGetDatabaseInfo` / `JetGetObjectInfo` / `JetGetIndexInfo` are
  covered)
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
- `JetGetCounter`, `JetResetCounter`

### Multi-instance / params
- `JetEnableMultiInstance`, `JetCreateInstance` (unversioned;
  `JetCreateInstance2` IS covered)
- `JetGetSystemParameter`, `JetGetResourceParam`, `JetSetResourceParam`
- `JetConfigureProcessForCrashDump`
- `JetGetErrorInfo` — structured error metadata
- `JetCreateEncryptionKey` (engine has libsodium stub)

### Tools / hooks
- `JetTracing`, `JetTestHook`, `JetDBUtilities`
- `JetPrepareToCommitTransaction`

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

**Items 1–9 from round 1 and items 1–8 from round 2 landed.**  Round 2
added `JetRetrieveKey`, `JetGetLock`, `JetGetRecordSize`,
`JetGetDatabaseInfo`, `JetGetObjectInfo`, `JetGetIndexInfo`,
`JetGetInstanceInfo`, `JetSetSessionContext` /
`JetResetSessionContext`, `JetSetLS` / `JetGetLS`,
`JetIndexRecordCount`.

Round 3 candidates from the remaining gap list:

1. **`JetGetCurrentIndex`** — companion to `JetSetCurrentIndex`
   (covered); reports the active index name on a cursor.
2. **`JetSetTableSequential` / `JetResetTableSequential`** — sequential
   scan hint; trivial scenario, covers the prefetch path.
3. **`JetSetCursorFilter`** — server-side row filter; pairs with
   navigation scenarios.
4. **`JetGetRecordPosition` / `JetGotoPosition`** — fraction-of-table
   navigation.
5. **`JetGetSecondaryIndexBookmark` / `JetGotoSecondaryIndexBookmark`** —
   cross-index navigation.
6. **`JetTruncateLog` / `JetTruncateLogInstance`** — log management.
7. **`JetGetLogInfoInstance`** — log-file metadata; complements the
   existing backup scenarios.
8. **`JetGetAttachInfo`** — global form (the *Instance* variant is
   covered by `BackupRestore.ExternalBackupExposesAttachInfo`).

## What's NOT in scope here

- **JetDBUtilities / JetTestHook / JetTracing** — engineering surface,
  not user-facing.  Coverage gap by design.
- **External backup (`JetExternalRestore`/`*RestoreInstance`)** — the
  *Instance* variant is already used in `BackupRestore.*`; the global
  forms are deprecated wrappers.
- **Versioned siblings of covered bases** — listed for completeness
  but not real coverage gaps; the underlying capability is exercised.
