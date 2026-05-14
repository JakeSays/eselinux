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

**Totals:** 212 base APIs declared, 158 covered (75%), 54 untested.

## Tested (158)

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

### DML
- `JetRetrieveColumnByReference`, `JetRetrieveColumnFromRecordStream`,
  `JetPrereadColumnsByReference`, `JetStreamRecords` — column-stream
  surface

### Database lifecycle
- `JetConvertDDL` — DB-level DDL migration

### Logs / replay
- `JetGetLogInfoInstance2` (covered via the BackupRestore round-4
  scenario)
- `JetConsumeLogData`, `JetExternalRestore`, `JetExternalRestore2`
- `JetBeginDatabaseIncrementalReseed`, `JetEndDatabaseIncrementalReseed`

### Page inspection
- `JetOnlinePatchDatabasePage`, `JetPatchDatabasePages`

### Snapshot extensions
- `JetOSSnapshotTruncateLog`, `JetOSSnapshotTruncateLogInstance` —
  blocked on engine investigation (hang inside
  `pSession->ErrTruncateLogs` on this Linux build)

### Revertable-Backup-Set (RBS)
- `JetRBSPrepareRevert`, `JetRBSExecuteRevert`, `JetRBSCancelRevert`,
  `JetGetRBSFileInfo`

### Service / housekeeping
- `JetStopService`, `JetStopServiceInstance`, `JetStopServiceInstance2`

### Multi-instance / params
- `JetConfigureProcessForCrashDump`

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

- **Round 5** (current): global file-access (`JetOpenFile`,
  `JetReadFile`, `JetCloseFile`), `JetGetLogInfo` (global),
  `JetOpenFileSectionInstance`, `JetRemoveLogfile`,
  `JetBeginSurrogateBackup`, `JetEndSurrogateBackup`,
  `JetBeginTransaction3`, `JetCommitTransaction2`, `JetUpdate2`,
  `JetSetCurrentIndex2`, `JetCreateIndex2`, `JetCreateDatabase2`,
  `JetAttachDatabase2`, `JetCreateTableColumnIndex2`,
  `JetOpenTempTable3`, `JetDefragment2`, `JetInit2`,
  `JetEnableMultiInstance` (forked-child via CrashHelper).

## Suggested round 6 candidates

The remaining genuine gaps are higher-cost or lower-value than what
round 5 picked up:

1. **`JetConvertDDL`** — opaque DDL-conversion path used during
   schema migration.  Needs a test database with the
   pre-conversion DDL on disk.
2. **`JetBeginDatabaseIncrementalReseed` / `JetEndDatabaseIncrementalReseed`**
   — partial-database reseed for replication recovery.  Complex
   protocol; needs a corrupted+rebuilt-from-source-instance setup.
3. **`JetOnlinePatchDatabasePage` / `JetPatchDatabasePages`** — page
   patch surface for replication.  Requires a corrupted page token
   + the patch bytes from a known-good replica.
4. **`JetConsumeLogData`** — feed log records into the engine from
   an external replication source.
5. **`JetOSSnapshotTruncateLog` / `JetOSSnapshotTruncateLogInstance`**
   — blocked on the engine hang noted in round 4.
6. **RBS surface** (`JetRBSPrepareRevert` etc.) — revertable-backup-
   set API; needs the RBS recovery protocol.
7. **`JetStopService`** family — graceful instance shutdown.
