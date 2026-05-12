# ese-tests

Public-API integration test suite for the ESE Linux port.

`ese-tests` exercises ESE through the published JET API only — `jet.h`,
`esent.h`, and the Win32 type shim in `windows-shim/`. Nothing under
`dev/ese/src/` is on its include path. The test binary links
`libese.so` and consumes the engine the same way an external customer
would.

## Build

The target lives at `repo/integration-tests/` and is wired into the
top-level CMake build under the `IntegrationTestsEnabled` option
(defaults to ON for non-Windows builds):

```
cd build           # or build-release
cmake --build . --target ese-tests -j14
```

The output is `bin/ese-tests`.

## Run

List every registered scenario:
```
bin/ese-tests --list
```

Run everything (default `--filter "*"`):
```
bin/ese-tests
```

Run a specific category or pattern:
```
bin/ese-tests --filter "Schema.*"
bin/ese-tests --filter "*RoundTrip*"
bin/ese-tests --filter "Database.CreateAndClose"
```

Workload size — affects row counts, table counts, long-value sizes in
scenarios that look up `RowCount()` / `TableCount()` /
`LongValueBytes()`. Default `Small`:
```
bin/ese-tests --scale Small      # fast (< 1 minute total)
bin/ese-tests --scale Medium     # nightly (< 10 minutes)
bin/ese-tests --scale Large      # opt-in stress (hours OK)
```

Keep scenario temp directories instead of deleting them on exit (useful
for inspecting databases after a failure):
```
bin/ese-tests --keep-temp
```

Show per-scenario start lines:
```
bin/ese-tests --verbose
```

## Layout

```
integration-tests/
├── CMakeLists.txt
├── README.md
└── src/
    ├── Main.cxx                     # argv parsing, runner orchestration
    ├── Framework/                   # RAII wrappers + harness
    │   ├── Check.hxx / .cxx
    │   ├── CrashHelper.hxx / .cxx   # fork + SIGKILL for recovery tests
    │   ├── DataGenerator.hxx / .cxx
    │   ├── EseDatabase.hxx / .cxx
    │   ├── EseInstance.hxx / .cxx
    │   ├── EseSession.hxx / .cxx
    │   ├── EseTable.hxx / .cxx
    │   ├── EseTransaction.hxx / .cxx
    │   ├── ScaleProfile.hxx / .cxx
    │   ├── Scenario.hxx / .cxx
    │   ├── ScenarioRegistry.hxx / .cxx
    │   ├── TemporaryDirectory.hxx / .cxx
    │   └── ThreadGroup.hxx / .cxx
    └── Scenarios/                   # one file per ScenarioCategory
        ├── PlatformScenarios.cxx
        ├── SessionScenarios.cxx
        ├── DatabaseScenarios.cxx
        ├── SchemaScenarios.cxx
        ├── DataManipulationScenarios.cxx
        ├── ColumnTypeScenarios.cxx
        ├── NavigationScenarios.cxx
        ├── TransactionScenarios.cxx
        ├── LongValueScenarios.cxx
        ├── MultiValueScenarios.cxx
        ├── TemporaryTableScenarios.cxx
        ├── EscrowScenarios.cxx
        ├── BackupRestoreScenarios.cxx
        ├── RecoveryScenarios.cxx
        ├── SnapshotScenarios.cxx
        ├── MaintenanceScenarios.cxx
        ├── LimitScenarios.cxx
        ├── ErrorScenarios.cxx
        ├── ScaleScenarios.cxx
        └── ConcurrencyScenarios.cxx
```

## Writing a scenario

Use the `EseIntegrationScenario` macro at the top of any
`Scenarios/*.cxx`:

```cpp
EseIntegrationScenario(Schema, AddColumnAfterTableCreation)
{
    ese::tests::TemporaryDirectory directory("Schema.AddColumnAfterTableCreation");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "Schema.mdb");
    ese::tests::EseTable           table(database, "Customers");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLong;

    JET_COLUMNID columnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Identity",
                           &columnDefinition,
                           nullptr,
                           0,
                           &columnId));
    Require(columnId != 0);
}
```

`CheckJet(expr)` throws `ScenarioFailure` on a negative `JET_ERR`.
`Require(condition)` throws if the predicate fails. `RequireJetError(expr, expected)`
asserts the call returned exactly the named error code.

## Isolation caveats

ESE has process-global state (the resource manager freezes its buffer
pool / cursor table / cache parameters on first commit). A fresh
`EseInstance` does **not** give a fresh engine. Scenarios that need
a pristine engine — recovery, parameter re-matrixing, crash recovery —
use the `CrashHelper` fork+exec path (one entry per such scenario,
registered via `RegisterChildEntry`).

## Phases

This is the Phase 1 skeleton. The category list is fixed at 20; each
file currently has one smoke case (or a placeholder where the deeper
machinery isn't online yet). Future phases fill out depth:

- Phase 2: DDL/DML/coltyps depth (Schema, DataManipulation, ColumnType,
  Navigation).
- Phase 3: Transaction, LongValue, MultiValue, TemporaryTable, Escrow.
- Phase 4: BackupRestore, Recovery (child + SIGKILL), Snapshot,
  Maintenance.
- Phase 5: Limit, Error matrix, Scale, Concurrency.
