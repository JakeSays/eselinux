// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <filesystem>
#include <string>
#include <string_view>

using namespace ese::tests;

EseIntegrationScenario(Database, CreateAndClose)
{
    TemporaryDirectory directory("Database.CreateAndClose");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Smoke.mdb");

    Require(database.Id() != JET_dbidNil);
    Require(std::filesystem::exists(database.Path()));
}

EseIntegrationScenario(Database, GrowDatabaseExtendsFileBySpecifiedPages)
{
    TemporaryDirectory directory(
        "Database.GrowDatabaseExtendsFileBySpecifiedPages");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Grow.mdb");

    constexpr uint32_t TargetPages = 256;
    uint32_t pagesReal = 0;
    CheckJet(JetGrowDatabase(session.Handle(), database.Id(),
                             TargetPages, &pagesReal));
    // JetGrowDatabase only extends — never shrinks — so the reported
    // size is at least the requested size.
    Require(pagesReal >= TargetPages);

    // Same call with a smaller target is a no-op and returns the
    // current larger size.
    uint32_t pagesAfterNoOp = 0;
    CheckJet(JetGrowDatabase(session.Handle(), database.Id(),
                             16, &pagesAfterNoOp));
    Require(pagesAfterNoOp == pagesReal);
}

EseIntegrationScenario(Database, SetDatabaseSizeMatchesGrowSemantics)
{
    TemporaryDirectory directory(
        "Database.SetDatabaseSizeMatchesGrowSemantics");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Sized.mdb");

    const auto databasePath = database.Path().string();
    constexpr uint32_t TargetPages = 200;

    uint32_t pagesReal = 0;
    // JetSetDatabaseSize requires the DB be detached (the call needs
    // exclusive access to the file).  Close + detach, set, then reattach.
    CheckJet(JetCloseDatabase(session.Handle(), database.Id(), 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), databasePath.c_str()));
    CheckJet(JetSetDatabaseSizeA(session.Handle(), databasePath.c_str(),
                                 TargetPages, &pagesReal));
    Require(pagesReal >= TargetPages);

    // Re-attach to confirm the file is still usable.
    CheckJet(JetAttachDatabaseA(session.Handle(), databasePath.c_str(), 0));
    JET_DBID reattachedDbid = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(), databasePath.c_str(),
                              nullptr, &reattachedDbid, 0));
    Require(reattachedDbid != JET_dbidNil);
    CheckJet(JetCloseDatabase(session.Handle(), reattachedDbid, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), databasePath.c_str()));
    // EseDatabase destructor will try to close the original dbid handle
    // we already closed; the framework swallows the JET_errInvalidDatabaseId
    // gracefully or its destructor is a no-op.  Either way the scenario
    // verified the API surface.
    database.Release();
}

EseIntegrationScenario(Database, SetMaxDatabaseSizeIsReadableViaGetMax)
{
    TemporaryDirectory directory(
        "Database.SetMaxDatabaseSizeIsReadableViaGetMax");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Capped.mdb");

    constexpr uint32_t CapPages = 1024;
    CheckJet(JetSetMaxDatabaseSize(session.Handle(), database.Id(),
                                   CapPages, 0));

    uint32_t observedCap = 0;
    CheckJet(JetGetMaxDatabaseSize(session.Handle(), database.Id(),
                                   &observedCap, 0));
    Require(observedCap == CapPages);
}

EseIntegrationScenario(Database, GetDatabaseInfoReportsFilenameAndSize)
{
    TemporaryDirectory directory(
        "Database.GetDatabaseInfoReportsFilenameAndSize");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Info.mdb");

    // JET_DbInfoFilename returns the absolute path as ASCII (we call
    // the A variant explicitly through the macro alias).
    char filenameBuffer[1024] = {};
    CheckJet(JetGetDatabaseInfoA(session.Handle(), database.Id(),
                                 filenameBuffer, sizeof(filenameBuffer),
                                 JET_DbInfoFilename));
    Require(filenameBuffer[0] != '\0');
    // The reported filename must end with the database name we created.
    const std::string_view reportedPath{filenameBuffer};
    Require(reportedPath.ends_with("Info.mdb"));

    // JET_DbInfoFilesize: 64-bit byte count of the database file.
    uint64_t fileSize = 0;
    CheckJet(JetGetDatabaseInfoA(session.Handle(), database.Id(),
                                 &fileSize, sizeof(fileSize),
                                 JET_DbInfoFilesize));
    Require(fileSize > 0);

    // JET_DbInfoPageSize: page size in bytes; the engine pins this
    // to 4 KB by default in this repo (see JET_paramDatabasePageSize).
    uint32_t pageSize = 0;
    CheckJet(JetGetDatabaseInfoA(session.Handle(), database.Id(),
                                 &pageSize, sizeof(pageSize),
                                 JET_DbInfoPageSize));
    Require(pageSize == 4096);
}
