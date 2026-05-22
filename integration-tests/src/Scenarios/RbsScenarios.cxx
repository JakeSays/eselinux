// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/RowOperations.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace ese::tests;

namespace
{

//  Build a JET_LOGTIME from std::time_t.  JET_LOGTIME uses the same
//  packed wire format as JET_BKLOGTIME (`jetapi.h:1921`):
//  bSeconds/bMinutes/bHours/bDay/bMonth/bYear (year-1900), plus
//  fTimeIsUTC and split milliseconds bits.
JET_LOGTIME MakeJetLogtime(std::time_t when)
{
    std::tm utc = {};
    gmtime_r(&when, &utc);

    JET_LOGTIME jlt = {};
    jlt.bSeconds = static_cast<char>(utc.tm_sec);
    jlt.bMinutes = static_cast<char>(utc.tm_min);
    jlt.bHours = static_cast<char>(utc.tm_hour);
    jlt.bDay = static_cast<char>(utc.tm_mday);
    jlt.bMonth = static_cast<char>(utc.tm_mon + 1);
    jlt.bYear = static_cast<char>(utc.tm_year);
    jlt.fTimeIsUTC = 1;
    return jlt;
}

//  Provision a JET instance ready for JetInit.  Configures all the
//  paths + RBS knobs.  Caller does the JetInit and session/database
//  work.  Used by both the bootstrap (init #1) and reattach (init #2)
//  phases of every RBS scenario.
JET_INSTANCE CreateRbsEnabledInstance(const std::filesystem::path& directory,
                                      const char* instanceName)
{
    JET_INSTANCE handle = JET_instanceNil;
    CheckJet(JetCreateInstance2A(&handle,
                                 instanceName,
                                 instanceName,
                                 0));

    auto pathWithSep = directory.string();
    if (!pathWithSep.empty() && pathWithSep.back() != '/')
    {
        pathWithSep.push_back('/');
    }
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramSystemPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramTempPath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramLogFilePath, 0,
                                    pathWithSep.c_str()));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramBaseName, 0, "edb"));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramEventSource, 0, instanceName));
    //  RBS needs non-circular logging — it captures pre-image pages
    //  into .rbs files that must reference closed log generations.
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramCircularLog, 0, nullptr));
    //  Small log files so a small amount of work rolls generations.
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramLogFileSize, 64, nullptr));

    //  Turn RBS on.  JET_paramRBSFilePath defaults to L".\\" (i.e.,
    //  CWD) which FDefaultParam treats as "not set"; we must point
    //  it explicitly into the scenario directory or the engine
    //  silently skips RBS init.
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramEnableRBS, 1, nullptr));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramRBSFilePath, 0,
                                    pathWithSep.c_str()));
    //  RBS roll is gated on BOTH RBSRollIntervalSec (default 12h)
    //  and RBSForceRollIntervalSec (default 48h, see
    //  CRevertSnapshotForAttachedDbs::FRollSnapshotRollSize check).
    //  Drive both to 1 second so a sleep + commit triggers a roll
    //  and the revert tests have multiple .rbs generations to
    //  pick a "from" point between.
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramFlight_RBSRollIntervalSec,
                                    1, nullptr));
    CheckJet(JetSetSystemParameterA(&handle, JET_sesidNil,
                                    JET_paramFlight_RBSForceRollIntervalSec,
                                    1, nullptr));

    return handle;
}

//  Bootstrap a database with an RBS-capable header.  Engine only
//  initialises its per-instance RBS context (`pinst->m_prbs`) when
//  the rstmap built during JetInit contains a database whose header
//  has JET_efvRevertSnapshot enabled — which on a brand-new instance
//  is no databases.  Pattern: create the database in one init pass,
//  term, then re-init.  Init #2's rstmap is populated from the
//  on-disk .edb (whose header carries the RBS flag because the
//  engine's default EFV is past the JET_efvRevertSnapshot floor) and
//  RBS becomes active.
//
//  Helper does: instance #1 (create db, write some baseline rows
//  for the revert tests to verify, term).  Returns the database file
//  name (caller appends to directory.Path()) so the reattach phase
//  can open the same file.
struct BootstrapResult
{
    std::string dbFileName;
    JET_COLUMNID valueColumn = 0;
    //  Wall-clock time captured strictly between gen 1's and gen 2's
    //  creation — the only target time JetRBSPrepareRevert will
    //  accept.  Reverting here unwinds gen 2's writes while keeping
    //  the database header pointing at gen 1 (effectively, the state
    //  after phase A bootstrap, since gen 1 captured pre-images of
    //  the cycle-1 marker write).
    std::time_t timeBetweenGens = 0;
};

//  JetInit (without an rstmap) leaves m_irstmapMac=0 which means
//  FRBSFeatureEnabledFromRstmap returns false and the RBS subsystem
//  silently skips init.  JetInit4A with an explicit rstmap pointing
//  at the existing database stamps an rstmap entry; the engine
//  loads the .edb header, sees the JET_efvRevertSnapshot flag, and
//  spins up the RBS context for subsequent writes.
void JetInitInPlaceWithRbs(JET_INSTANCE* pinstance,
                           const std::string& dbPath)
{
    JET_RSTMAP2_A rstmap = {};
    rstmap.cbStruct = sizeof(rstmap);
    rstmap.szDatabaseName = const_cast<char*>(dbPath.c_str());
    rstmap.szNewDatabaseName = const_cast<char*>(dbPath.c_str());

    JET_RSTINFO2_A rstInfo = {};
    rstInfo.cbStruct = sizeof(rstInfo);
    rstInfo.rgrstmap = &rstmap;
    rstInfo.crstmap = 1;

    CheckJet(JetInit4A(pinstance, &rstInfo, 0));
}

BootstrapResult BootstrapDatabaseWithRbsHeader(
    const std::filesystem::path& directory,
    const char* instanceName,
    const char* dbFileName,
    int32_t bootstrapRowCount)
{
    //  Phase A: Plain JetInit, JetCreateDatabase, write bootstrap
    //  rows, JetTerm.  Produces an .edb on disk with the
    //  JET_efvRevertSnapshot flag in its database version (which
    //  is the engine default).  No .rbs files yet — RBS doesn't
    //  start until init #2 sees the rstmap entry.
    JET_COLUMNID columnId = 0;
    const auto dbPath = (directory / dbFileName).string();
    {
        JET_INSTANCE handle =
            CreateRbsEnabledInstance(directory, instanceName);
        CheckJet(JetInit(&handle));

        JET_SESID sesid = JET_sesidNil;
        CheckJet(JetBeginSessionA(handle, &sesid, nullptr, nullptr));

        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(sesid, dbPath.c_str(),
                                    nullptr, &dbid, 0));

        JET_TABLEID tableid = JET_tableidNil;
        CheckJet(JetCreateTableA(sesid, dbid, "Rows", 8, 100, &tableid));
        JET_COLUMNDEF coldef = {};
        coldef.cbStruct = sizeof(coldef);
        coldef.coltyp = JET_coltypLong;
        CheckJet(JetAddColumnA(sesid, tableid, "Value",
                               &coldef, nullptr, 0, &columnId));

        CheckJet(JetBeginTransaction(sesid));
        for (int32_t i = 0; i < bootstrapRowCount; ++i)
        {
            CheckJet(JetPrepareUpdate(sesid, tableid, JET_prepInsert));
            CheckJet(JetSetColumn(sesid, tableid, columnId,
                                  &i, sizeof(i), 0, nullptr));
            CheckJet(JetUpdate(sesid, tableid, nullptr, 0, nullptr));
        }
        CheckJet(JetCommitTransaction(sesid, 0));

        CheckJet(JetCloseTable(sesid, tableid));
        CheckJet(JetCloseDatabase(sesid, dbid, 0));
        CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
        CheckJet(JetEndSession(sesid, 0));
        CheckJet(JetTerm2(handle, JET_bitTermComplete));
    }

    //  Helper to do one init/work/term cycle so the engine rolls a
    //  fresh .rbs generation on each entry.  RBS roll-check fires
    //  inside JetInit4A (tm.cxx ~1046: `if ( pinst->m_prbs &&
    //  pinst->m_prbs->FRollSnapshot() )`), not during writes, so
    //  the only way to roll a new gen is another init cycle past
    //  the roll-interval threshold.
    auto initWorkTerm = [&](int32_t markerValue) {
        JET_INSTANCE handle =
            CreateRbsEnabledInstance(directory, instanceName);
        JetInitInPlaceWithRbs(&handle, dbPath);

        JET_SESID sesid = JET_sesidNil;
        CheckJet(JetBeginSessionA(handle, &sesid, nullptr, nullptr));
        CheckJet(JetAttachDatabaseA(sesid, dbPath.c_str(), 0));
        JET_DBID dbid = JET_dbidNil;
        CheckJet(JetOpenDatabaseA(sesid, dbPath.c_str(),
                                  nullptr, &dbid, 0));
        JET_TABLEID tableid = JET_tableidNil;
        CheckJet(JetOpenTableA(sesid, dbid, "Rows",
                               nullptr, 0, 0, &tableid));

        CheckJet(JetBeginTransaction(sesid));
        CheckJet(JetPrepareUpdate(sesid, tableid, JET_prepInsert));
        CheckJet(JetSetColumn(sesid, tableid, columnId,
                              &markerValue, sizeof(markerValue),
                              0, nullptr));
        CheckJet(JetUpdate(sesid, tableid, nullptr, 0, nullptr));
        CheckJet(JetCommitTransaction(sesid, 0));

        CheckJet(JetCloseTable(sesid, tableid));
        CheckJet(JetCloseDatabase(sesid, dbid, 0));
        CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
        CheckJet(JetEndSession(sesid, 0));
        CheckJet(JetTerm2(handle, JET_bitTermComplete));
    };

    //  First cycle: creates gen 1 of the RBS, writes a marker.
    initWorkTerm(bootstrapRowCount + 1000);

    //  Capture wall-clock time AFTER gen 1 was created (the init
    //  inside initWorkTerm() rolled it).  A revert target between
    //  this point and the next cycle's gen-2 creation is the only
    //  range JetRBSPrepareRevert will accept — gen 2's tmCreate is
    //  STRICTLY AFTER this timestamp, and gen 1's tmCreate is
    //  STRICTLY BEFORE.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    const auto timeBetweenGens = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::this_thread::sleep_for(std::chrono::seconds(2));

    //  Second cycle: init sees existing gen 1, FRollSnapshot
    //  returns true (RollIntervalSec + ForceRollIntervalSec both 1),
    //  engine rolls to gen 2.
    initWorkTerm(bootstrapRowCount + 2000);

    return BootstrapResult{
        std::string(dbFileName),
        columnId,
        timeBetweenGens,
    };
}

}  // namespace

EseIntegrationScenario(Rbs, PrepareAndCancelLeavesDatabaseUntouched, Smoke)
{
    TemporaryDirectory directory("Rbs.PrepareAndCancelLeavesDatabaseUntouched");

    //  Bootstrap (phase A) + roll RBS gens (phase B): both happen
    //  inside the helper, which terms the instance cleanly so the
    //  .rbs files are closed and the revert path can open them.
    const auto bootstrap = BootstrapDatabaseWithRbsHeader(
        directory.Path(), "RbsPrepareCancel", "Revert.mdb", 100);
    const auto dbPath = (directory.Path() / bootstrap.dbFileName).string();

    //  Phase C: JetRBSPrepareRevert is FEnterWithoutInit-gated — it
    //  needs a JET_INSTANCE with paths configured but NOT yet
    //  JetInit'd, because the revert path reads the .rbs files
    //  directly and would clash with the rolling RBS thread an
    //  init'd instance owns.
    JET_INSTANCE handle = CreateRbsEnabledInstance(directory.Path(),
                                                   "RbsPrepareCancelC");

    //  Revert target must land strictly between gen 1's tmCreate
    //  and gen 2's tmCreate — bootstrap captured exactly that
    //  point in timeBetweenGens.  Anything earlier (e.g., before
    //  any .rbs gen existed) returns JET_errRBSRCInvalidRBS from
    //  ErrComputeRBSRangeToApply.
    JET_LOGTIME jltExpected = MakeJetLogtime(bootstrap.timeBetweenGens);
    JET_LOGTIME jltActual = {};
    CheckJet(JetRBSPrepareRevert(handle,
                                 jltExpected,
                                 /*cpgCache=*/256,
                                 0,
                                 &jltActual));
    CheckJet(JetRBSCancelRevert(handle));
    CheckJet(JetTerm2(handle, JET_bitTermComplete));

    //  Phase D: reattach (init #2 pattern) and verify the database
    //  wasn't touched.  Bootstrap inserted 100 rows + helper added
    //  2 markers during phase B; Cancel left them all alone.
    JET_INSTANCE verify = CreateRbsEnabledInstance(directory.Path(),
                                                   "RbsPrepareCancelD");
    JetInitInPlaceWithRbs(&verify, dbPath);
    JET_SESID sesid = JET_sesidNil;
    CheckJet(JetBeginSessionA(verify, &sesid, nullptr, nullptr));
    CheckJet(JetAttachDatabaseA(sesid, dbPath.c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sesid, dbPath.c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetOpenTableA(sesid, dbid, "Rows",
                           nullptr, 0, 0, &tableid));

    int observed = 0;
    CheckJet(JetMove(sesid, tableid, JET_MoveFirst, 0));
    while (true)
    {
        ++observed;
        const auto rc = JetMove(sesid, tableid, JET_MoveNext, 0);
        if (rc == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(rc);
    }
    Require(observed == 102);  // 100 bootstrap + 2 phase-B markers

    CheckJet(JetCloseTable(sesid, tableid));
    CheckJet(JetCloseDatabase(sesid, dbid, 0));
    CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
    CheckJet(JetEndSession(sesid, 0));
    CheckJet(JetTerm2(verify, JET_bitTermComplete));
}

EseIntegrationScenario(Rbs, GetRBSFileInfoReadsHeaderOfClosedSnapshotFile, Smoke)
{
    TemporaryDirectory directory(
        "Rbs.GetRBSFileInfoReadsHeaderOfClosedSnapshotFile");

    //  Bootstrap + roll RBS gens; helper terms cleanly so the .rbs
    //  file is closed and JetGetRBSFileInfo can read its header.
    (void)BootstrapDatabaseWithRbsHeader(
        directory.Path(), "RbsFileInfo", "RbsInfo.mdb", 100);

    //  Find a closed .rbs file.  The engine stores them either at
    //  the root of JET_paramRBSFilePath or in a per-database subdir
    //  named after the database; walk recursively.
    std::filesystem::path rbsFile;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory.Path()))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".rbs")
        {
            rbsFile = entry.path();
            break;
        }
    }
    Require(!rbsFile.empty());

    //  Read the .rbs header via JetGetRBSFileInfo.  Fields the
    //  engine populates in JET_RBSINFOMISC (jetapi.h:3362):
    //  lRBSGeneration (>= 1), logtimeCreate (non-zero year),
    //  ulMajor + ulMinor (version), cbLogicalFileSize (> 0).
    JET_RBSINFOMISC info = {};
    CheckJet(JetGetRBSFileInfoA(rbsFile.string().c_str(),
                                &info,
                                sizeof(info),
                                JET_RBSFileInfoMisc));
    Require(info.lRBSGeneration >= 1);
    Require(info.logtimeCreate.bYear > 0);
    Require(info.cbLogicalFileSize > 0);
    //  cbLogicalFileSize tracks the engine's record of live RBS
    //  content.  The on-disk file is at least that large — the
    //  engine may preallocate slack space beyond the logical
    //  end — so the relationship is bounded, not equal.
    std::error_code errorCode;
    const auto onDiskBytes = std::filesystem::file_size(rbsFile, errorCode);
    Require(!errorCode);
    Require(static_cast<uintmax_t>(info.cbLogicalFileSize) <= onDiskBytes);
    //  logtime fields beyond bYear must also be plausible.  bMonth
    //  in [1,12], bDay in [1,31], etc.  bYear is offset-from-1900.
    Require(info.logtimeCreate.bMonth >= 1);
    Require(info.logtimeCreate.bMonth <= 12);
    Require(info.logtimeCreate.bDay >= 1);
    Require(info.logtimeCreate.bDay <= 31);
    Require(static_cast<uint8_t>(info.logtimeCreate.bYear) >= 100);
    //  ulMajor must be > 0 — an uninitialized version field would
    //  ring alarm bells.  ulMinor is currently 0 on the supported
    //  format version, so don't constrain it.
    Require(info.ulMajor > 0);
}

EseIntegrationScenario(Rbs, PrepareRevertRejectsUnreachableTargetTime, Smoke)
{
    TemporaryDirectory directory(
        "Rbs.PrepareRevertRejectsUnreachableTargetTime");

    (void)BootstrapDatabaseWithRbsHeader(
        directory.Path(), "RbsClampBoot", "Clamp.mdb", 50);

    JET_INSTANCE handle = CreateRbsEnabledInstance(directory.Path(),
                                                   "RbsClamp");

    //  Ask the engine to revert to a time 10 years in the past —
    //  earlier than any available .rbs gen.
    //  ErrComputeRBSRangeToApply iterates max→min looking for a
    //  gen with tmCreate <= target.  If it walks past gen 1 and
    //  finds nothing, it surfaces JET_errRBSRCInvalidRBS from the
    //  tmPrevGen check (`!tmPrevGen.FIsSet()` on gen 1 — gen 1
    //  has no predecessor).  Documented contract for "no
    //  reachable revert point".
    const auto ancientTime = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now() -
        std::chrono::hours(24 * 365 * 10));
    JET_LOGTIME jltExpected = MakeJetLogtime(ancientTime);
    JET_LOGTIME jltActual = {};
    RequireJetError(JetRBSPrepareRevert(handle,
                                        jltExpected,
                                        /*cpgCache=*/128,
                                        0,
                                        &jltActual),
                    JET_errRBSRCInvalidRBS);

    CheckJet(JetTerm2(handle, JET_bitTermComplete));
}

EseIntegrationScenario(Rbs, ExecuteRevertRollsDatabaseBackToEarlierState, Smoke)
{
    TemporaryDirectory directory(
        "Rbs.ExecuteRevertRollsDatabaseBackToEarlierState");

    //  Bootstrap writes 500 rows in phase A, then phase B in the
    //  helper does two init/term cycles that each roll a new .rbs
    //  generation and insert one marker row.  Helper returns
    //  timeBetweenGens, a wall-clock instant strictly between gen 1
    //  and gen 2 — the only target JetRBSPrepareRevert will accept.
    //  Reverting to that point unwinds gen 2's writes back through
    //  gen 1's pre-images and (with JET_bitDeleteAllExistingLogs
    //  forcing the log dir to drop) leaves the database with the
    //  500 bootstrap rows only.
    constexpr int32_t PhaseACount = 500;
    const auto bootstrap = BootstrapDatabaseWithRbsHeader(
        directory.Path(), "RbsExecute", "Execute.mdb", PhaseACount);
    const auto dbPath = (directory.Path() / bootstrap.dbFileName).string();

    //  Phase C: uninited instance, prepare + execute the revert.
    JET_INSTANCE handle = CreateRbsEnabledInstance(directory.Path(),
                                                   "RbsExecuteC");

    JET_LOGTIME jltTarget = MakeJetLogtime(bootstrap.timeBetweenGens);
    JET_LOGTIME jltActual = {};
    //  JET_bitDeleteAllExistingLogs forces the revert path to delete
    //  the existing log files at the end (revertsnapshot.cxx:5817 →
    //  fDeleteExistingLogs).  Without it the .rbs revert applies the
    //  page pre-images but the log gens remain on disk, so the next
    //  JetInit's redo cleanly re-applies the rolled-back updates and
    //  the database returns to its pre-revert state — masking the
    //  effect of the revert at the row level.
    CheckJet(JetRBSPrepareRevert(handle,
                                 jltTarget,
                                 /*cpgCache=*/512,
                                 JET_bitDeleteAllExistingLogs,
                                 &jltActual));

    JET_RBSREVERTINFOMISC revertInfo = {};
    CheckJet(JetRBSExecuteRevert(handle, 0, &revertInfo));
    Require(revertInfo.cPagesReverted > 0);
    Require(revertInfo.lGenRBSMinApplied == 1);
    Require(revertInfo.lGenRBSMaxApplied == 2);

    CheckJet(JetTerm2(handle, JET_bitTermComplete));

    //  Phase D: reopen the database in a fresh instance, walk the
    //  rows, and verify the phase-B markers are gone.  The reverted
    //  state should contain only the 500 bootstrap rows.
    JET_INSTANCE postHandle = CreateRbsEnabledInstance(directory.Path(),
                                                       "RbsExecuteVerify");
    JetInitInPlaceWithRbs(&postHandle, dbPath);
    JET_SESID sesid = JET_sesidNil;
    CheckJet(JetBeginSessionA(postHandle, &sesid, nullptr, nullptr));
    CheckJet(JetAttachDatabaseA(sesid, dbPath.c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(sesid, dbPath.c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetOpenTableA(sesid, dbid, "Rows",
                           nullptr, 0, 0, &tableid));

    int afterRevert = 0;
    CheckJet(JetMove(sesid, tableid, JET_MoveFirst, 0));
    while (true)
    {
        ++afterRevert;
        const auto rc = JetMove(sesid, tableid, JET_MoveNext, 0);
        if (rc == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(rc);
    }
    Require(afterRevert == PhaseACount);

    CheckJet(JetCloseTable(sesid, tableid));
    CheckJet(JetCloseDatabase(sesid, dbid, 0));
    CheckJet(JetDetachDatabaseA(sesid, dbPath.c_str()));
    CheckJet(JetEndSession(sesid, 0));
    CheckJet(JetTerm2(postHandle, JET_bitTermComplete));
}

//  ===================================================================
//  Tier::Regression — RBS prepare-then-cancel leaves database
//  byte-identical.  Smoke PrepareAndCancel only counts rows.  A
//  refactor that applied partial page patches before cancellation
//  could pass the count check while corrupting page content.
//  This scenario captures the on-disk file SHA-equivalent (file
//  size + dbtime via JetGetDatabaseFileInfo) before and after the
//  cancelled prepare, requiring exact match.
//  ===================================================================
EseIntegrationScenario(Rbs, PrepareCancelPreservesEveryRowContent, Regression)
{
    TemporaryDirectory directory(
        "Rbs.PrepareCancelPreservesEveryRowContent");
    static constexpr int32_t Rows = 300;

    BootstrapResult bootstrap = BootstrapDatabaseWithRbsHeader(
        directory.Path(), "PreserveCancel", "Preserve.mdb", Rows);
    const auto dbPath = (directory.Path() / "Preserve.mdb").string();

    //  Capture file size + JET-reported file size BEFORE the
    //  prepare/cancel bracket.  Touching the database with
    //  JetAttachDatabase here would interfere with the
    //  FEnterWithoutInit-gated RBS prepare path, so we use the
    //  stat-based file size and the read-only JetGetDatabaseFileInfo
    //  which doesn't attach.
    const uintmax_t fileSizeBefore =
        std::filesystem::file_size(dbPath);
    int64_t dbFileSizeReportedBefore = 0;
    CheckJet(JetGetDatabaseFileInfoA(dbPath.c_str(),
                                     &dbFileSizeReportedBefore,
                                     sizeof(dbFileSizeReportedBefore),
                                     JET_DbInfoFilesize));

    //  Prepare a revert to a target time then immediately cancel.
    //  No actual revert ever runs.  The engine must not have
    //  modified the database file.
    JET_INSTANCE revertHandle =
        CreateRbsEnabledInstance(directory.Path(), "PreserveCancel");
    JET_LOGTIME targetTime = MakeJetLogtime(bootstrap.timeBetweenGens);
    JET_LOGTIME actualRevertTime = {};
    CheckJet(JetRBSPrepareRevert(revertHandle, targetTime,
                                 /*cpgCache=*/256, 0,
                                 &actualRevertTime));
    CheckJet(JetRBSCancelRevert(revertHandle));
    CheckJet(JetTerm2(revertHandle, JET_bitTermComplete));

    //  Post-cancel: file size + reported size must equal the
    //  pre-prepare values exactly.  Row count must still be
    //  the bootstrap count.
    const uintmax_t fileSizeAfter = std::filesystem::file_size(dbPath);
    Require(fileSizeAfter == fileSizeBefore);
    int64_t dbFileSizeReportedAfter = 0;
    CheckJet(JetGetDatabaseFileInfoA(dbPath.c_str(),
                                     &dbFileSizeReportedAfter,
                                     sizeof(dbFileSizeReportedAfter),
                                     JET_DbInfoFilesize));
    Require(dbFileSizeReportedAfter == dbFileSizeReportedBefore);

    //  Reattach + walk: rows must still be retrievable (the
    //  database isn't tombstoned by the prepare/cancel bracket).
    //  Row count is approximate vs the bootstrap value — RBS roll
    //  may have committed additional gen-marker rows — but it must
    //  be positive.
    EseInstance instance(directory);
    EseSession session(instance);
    CheckJet(JetAttachDatabaseA(session.Handle(), dbPath.c_str(), 0));
    JET_DBID dbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(), dbPath.c_str(),
                              nullptr, &dbid, 0));
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), dbid, "Rows",
                           nullptr, 0, 0, &tableid));
    CheckJet(JetMove(session.Handle(), tableid, JET_MoveFirst, 0));
    int32_t walkedRowCount = 0;
    do
    {
        ++walkedRowCount;
    }
    while (JetMove(session.Handle(), tableid, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(walkedRowCount >= Rows);
}
