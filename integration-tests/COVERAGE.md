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

**Totals:** 212 base APIs declared, 171 covered (81%), 41 untested.

## Tested (171)

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
- `JetConsumeLogData`, `JetExternalRestore`, `JetExternalRestore2`
  (round 8 — replication)
- `JetBeginDatabaseIncrementalReseed`, `JetEndDatabaseIncrementalReseed`
  (round 8 — replication)

### Page inspection
- `JetOnlinePatchDatabasePage`, `JetPatchDatabasePages` (round 8 —
  replication page repair)

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

- **Round 7** (current): RBS / revert-snapshot surface —
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

## Suggested round 8 candidates

**Replication surface** — the engine's log-shipping and replica-repair
APIs.  Round 8 is the heaviest round on the roadmap: every replica is
a **separate OS process** with its own engine, communicating with
peers over **Unix domain sockets**.  No multi-instance shortcuts —
this is the topology real Exchange DAG deployments use (one engine
per host) compressed onto a single box.

APIs to cover:
- `JetConsumeLogData` — passive side of log shipping; feeds bytes
  captured from the active back into a local engine.
- `JET_paramEmitLogDataCallback` / `EmitLogDataCallbackCtx` +
  `JET_PFNEMITLOGDATA` (callback type) — active side; engine fires
  the callback whenever the log writer flushes, rolls a
  generation, or starts/stops the stream.  The callback's body is
  what writes the wire-protocol frames onto the UDS.
- `JetPatchDatabasePages` — offline page repair against a closed
  database file (used when recovery itself can't proceed because
  of a header-path corruption).
- `JetOnlinePatchDatabasePage` — online single-page repair against
  a live database, with a token issued by the active so a stale
  shipment can't clobber an already-healed page.
- `JetBeginDatabaseIncrementalReseed` /
  `JetEndDatabaseIncrementalReseed` — generation-level divergence
  repair when active and passive forked at some log gen.
- `JetExternalRestore` / `JetExternalRestore2` — cold-restore a
  full replica from a peer's captured files, with a
  `JET_RSTMAP`/`JET_RSTMAP2` path remapping.

### Process topology

Each scenario spins up two or more **child processes** via the
existing `ChildProcess` machinery in `CrashHelper`:

```
parent (scenario body)
  ├─ fork+exec child "active"  — owns ActiveDb.mdb in its TemporaryDirectory subdir
  ├─ fork+exec child "passive1" — owns Passive1.mdb in its subdir
  ├─ fork+exec child "passive2" — owns Passive2.mdb in its subdir (optional)
  └─ wait for all children to exit cleanly
```

- Each child gets its own **scratch directory** under the scenario's
  `TemporaryDirectory` (no shared on-disk state — replication is
  about the engines being totally independent and meeting only on
  the wire).
- Each child is a normal single-instance `EseInstance` — no
  `JetEnableMultiInstance` complications, the whole framework
  carries over.
- The parent's job is **orchestrator**: pick UDS paths, spawn
  children with role + socket-path arguments, monitor exits.

### Wire protocol (proposed `replication-wire.hxx`)

A tiny framed protocol over `SOCK_STREAM` UDS, little-endian, no
authentication (this is loopback-only, single-host).  Each frame:

```
struct ReplicationFrame {
    uint32_t kind;          // ReplicationFrameKind
    uint32_t payloadBytes;  // bytes that follow this header
    // payload follows: layout determined by kind
};

enum class ReplicationFrameKind : uint32_t {
    // Active → passive (log shipping)
    LogData = 1,            // payload: JET_EMITDATACTX || cbLogData || raw log bytes
    StreamComplete = 2,     // no payload — active is shutting down its emit channel

    // Active ↔ passive (identity + checkpoint)
    LogSignatureQuery = 10, // passive asks active for its log signature
    LogSignatureReply = 11, // payload: JET_SIGNATURE
    CheckpointQuery = 12,
    CheckpointReply = 13,   // payload: lgpos QWORD

    // Active → passive (page repair)
    PageReadRequest = 20,   // payload: pgnoStart, cpg
    PageReadReply = 21,     // payload: cpg, raw page bytes (aligned)
    OnlinePatchRequest = 22,// payload: pgno, token bytes, page bytes

    // Incremental reseed handshake
    DivergedLogReport = 30, // active → passive: "your stream diverges from gen N"
    ReseedPatchRequest = 31,// passive → active: "send me page P"
    ReseedComplete = 32,    // active confirms all pages shipped

    // Test signalling
    Ready = 100,            // child → parent (via separate sentinel file, not UDS)
    AssertionFailure = 101, // child → parent: payload is UTF-8 error message
};
```

The framework provides:
- `ReplicationServer` — listens on a UDS path, accepts one peer.
- `ReplicationClient` — connects to a UDS path.
- `ReplicationChannel` — full-duplex helper around an accepted/connected
  fd; `SendFrame(kind, span<const byte>)` + `RecvFrame()`.

All frame I/O is **blocking** with an explicit timeout — easier to
debug than async; replication scenarios run in well-defined
phases.  Cancellation = close the fd, peer's next read returns 0.

### Active-child structure

```cpp
void RunActiveChild(const std::filesystem::path& directory,
                    const std::string& uplinkSocketPath)
{
    ReplicationServer server(uplinkSocketPath);
    ReplicationChannel channel = server.AcceptOne(std::chrono::seconds(10));

    JET_INSTANCE handle = ...;  // standard EseInstance pattern
    // Pre-init params:
    //   JET_paramEmitLogDataCallback     = &EmitCallback
    //   JET_paramEmitLogDataCallbackCtx  = &channel
    // EmitCallback's job: serialise JET_EMITDATACTX + the log buffer
    //                     into a LogData frame and SendFrame on channel.
    // Also handles control frames inbound (page reads, etc.) on a
    // worker thread.

    // ... insert rows, commit, term ...
    channel.SendFrame(ReplicationFrameKind::StreamComplete, {});
}
```

### Passive-child structure

```cpp
void RunPassiveChild(const std::filesystem::path& directory,
                     const std::string& uplinkSocketPath)
{
    ReplicationClient client(uplinkSocketPath);
    ReplicationChannel channel = client.Connect(std::chrono::seconds(10));

    // Standard EseInstance; configure a database that will receive
    // shipped log data.  CRITICAL: passive must JetCreateInstance with
    // the *same log signature* as the active before it can consume
    // the active's log bytes.  Use JetSetSystemParameter with
    // JET_paramLogSignature (if exposed) or seed via a captured
    // initial snapshot.

    while (true)
    {
        auto frame = channel.RecvFrame();
        if (frame.kind == ReplicationFrameKind::StreamComplete) break;
        if (frame.kind == ReplicationFrameKind::LogData)
        {
            // Deserialise JET_EMITDATACTX + payload, hand to engine.
            JetConsumeLogData(handle, &ctx, pvLogData, cbLogData, 0);
        }
        // ... handle other frame kinds ...
    }
}
```

### Scenarios

1. **`Replication.LogShippingActiveToOnePassive`** — active commits N
   rows.  Passive child reads each LogData frame, calls
   `JetConsumeLogData`.  After active sends StreamComplete and both
   children exit, parent attaches the passive's database read-only
   and verifies all N rows are present.
2. **`Replication.LogShippingActiveToTwoPassives`** — active fans
   out the same emit buffer to two independent passive sockets
   (one frame, two sends).  Both replicas converge to the same row
   set.  Tests that a single emit can drive multiple passives —
   the DAG fan-out pattern.
3. **`Replication.CheckpointAdvancesOnPassive`** — passive
   periodically issues `CheckpointQuery` to the active and
   compares against its own
   `JetGetInstanceMiscInfo(JET_InstanceMiscInfoCheckpoint)`.
   Passive's checkpoint must advance monotonically and never lead
   the active's.
4. **`Replication.LogSignatureMismatchRejectsForeignConsume`** —
   spin up two independent active children (separate signatures);
   a passive that was seeded from active A connects to active B
   and tries to consume.  Engine on the passive must reject with
   a signature-mismatch error.
5. **`Replication.OnlinePatchHealsCorruptedPageAcrossSocket`** —
   active populates + ships normally.  Passive child closes its
   engine, scenario flips one byte in a data page of the
   passive's `.edb`, passive re-opens.  Passive issues a
   PageReadRequest over the UDS, active replies with the good
   bytes from `JetGetDatabasePages`, passive applies via
   `JetOnlinePatchDatabasePage`.  Verify the corrupted-then-healed
   row reads correctly.
6. **`Replication.OfflinePatchRepairsClosedReplica`** — same
   corruption scenario, but the passive applies via
   `JetPatchDatabasePages` against the *closed* `.edb`.
7. **`Replication.IncrementalReseedAcrossDivergedActives`** —
   start with active+passive in sync.  Kill the active, promote
   the passive (now the "new active") for a few writes.  Bring
   the original active back as the new passive; original's stream
   has diverged.  Run the incremental-reseed protocol:
   `JetBeginDatabaseIncrementalReseed(divergedGen)` →
   PageReadRequests for the divergent range → `End*` with the new
   required-log window.  Verify the original-active-now-passive
   converges with the new active.
8. **`Replication.ExternalRestoreReplacesFailedActive`** — active
   produces a backup using `JetBackupInstanceA` (already covered).
   Active's directory is then `rm -rf`'d (simulated total loss).
   A passive child receives the backup files over the UDS,
   writes them to its own directory, calls `JetExternalRestore`
   with a `JET_RSTMAP` remapping, attaches the restored DB,
   verifies it's queryable.
9. **`Replication.ThreeNodeChainAToBToC`** — three children:
   - A: active, emits to socket-AB.
   - B: passive of A on socket-AB AND active for C on socket-BC.
     B installs its own emit callback that re-frames received
     LogData into outbound frames.
   - C: passive of B on socket-BC.
   Insert N rows on A; verify after stream-complete that C has
   all N rows.  Tests cascaded replication — Exchange DAGs deploy
   this pattern when bandwidth between datacenters is constrained.
10. **`Replication.PassiveDeathDuringShippingTriggersChannelClose`**
    — kill the passive child mid-stream while active is still
    emitting; active's `SendFrame` should return an error, active
    cleans up and exits.  Parent verifies neither child leaks.

### Framework deliverables before scenarios

In priority order:
1. **`Framework/ReplicationWire.hxx` + `.cxx`** — frame layout,
   `ReplicationServer`/`Client`/`Channel`.  Roughly 150–200 LOC,
   self-contained (no JET dependency).
2. **`Framework/ReplicationNode.hxx`** — child-entry helpers that
   wrap the active and passive role above.  Each child entry uses
   the existing `RegisterChildEntry` mechanism so the test binary
   re-exec'd in `--child-entry <name>` mode dispatches into the
   right role.
3. **`Framework/ReplicationOrchestrator.hxx`** — parent-side
   spawn-and-wait helper.  Takes a list of `(role, socket-path,
   directory)` and forks a child for each, then `WaitForExit` on
   all of them.  A non-zero exit on any child is a scenario failure.
4. **`Scenarios/ReplicationScenarios.cxx`** — the ten scenarios
   above.

### Open engineering questions for round 8

- **Seeding a passive with the right log signature.**  A passive can
  only consume an active's log bytes if its instance was seeded with
  the same `JET_SIGNATURE`.  Three candidate approaches:
    (a) the orchestrator does an initial full backup on the active
        and copies it into each passive's directory before the
        passive child starts;
    (b) the passive starts with no database, the active's first
        emit frame includes the database file header, and the
        passive writes that header out before opening the engine;
    (c) some form of `JetSetSystemParameter` lets us set the log
        signature directly — needs investigation.
  Approach (a) is closest to how Exchange does it; (b) is closer to
  what the wire protocol can naturally express.
- **Emit callback runs on the engine's log-writer thread.**  The
  callback body has to be thread-safe with respect to other engine
  callbacks and quick (it stalls log writes).  The UDS `send()` is
  probably fine but worth measuring.
- **Active's response to control frames** (page reads, checkpoint
  query) must happen on a *different* thread from the emit
  callback — the emit callback is on the log-writer thread which
  can't issue further JET API calls.  Easiest: spawn a single
  worker thread inside the active child that owns the channel-recv
  side and dispatches `PageReadRequest` etc. against a separate
  session.
- **Cleanup on assertion failure.**  Children may panic mid-stream.
  Parent must reap them all.  Existing `ChildProcess` destructor
  SIGKILLs uncreaped children — good enough.
