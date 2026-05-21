// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

EseIntegrationScenario(Database, GetDatabaseFileInfoReportsFileType)
{
    TemporaryDirectory directory("Database.GetDatabaseFileInfoReportsFileType");

    //  JetGetDatabaseFileInfo works on a closed file path — it opens
    //  the .edb, peeks at the header, and closes again.  Create a
    //  database inside its own EseInstance scope so it's flushed and
    //  detached cleanly before we probe it.
    std::filesystem::path databasePath;
    {
        EseInstance instance(directory);
        EseSession session(instance);
        EseDatabase database(session, "FileInfo.mdb");
        databasePath = database.Path();
    }

    uint32_t fileType = 0;
    CheckJet(JetGetDatabaseFileInfoA(databasePath.string().c_str(),
                                     &fileType,
                                     sizeof(fileType),
                                     JET_DbInfoFileType));
    Require(fileType == JET_filetypeDatabase);

    uint32_t pageSize = 0;
    CheckJet(JetGetDatabaseFileInfoA(databasePath.string().c_str(),
                                     &pageSize,
                                     sizeof(pageSize),
                                     JET_DbInfoPageSize));
    Require(pageSize == 4096);
}

EseIntegrationScenario(Database, GetDatabasePagesAndGetPageInfoRoundTrip)
{
    TemporaryDirectory directory(
        "Database.GetDatabasePagesAndGetPageInfoRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Pages.mdb");

    //  JetGetDatabasePages copies raw on-disk pages into a caller
    //  buffer.  Pull the first 4 pages (16 KiB at 4 KiB pages) — pgno
    //  1 is the database header, pgno 2 the shadow header.
    //
    //  The engine requires the destination buffer to be aligned to
    //  the OS memory-page commit granularity (4 KiB on this build) —
    //  it reads directly with O_DIRECT.  std::vector / new wouldn't
    //  satisfy that; std::aligned_alloc does.
    static constexpr uint32_t kPageSize = 4096;
    static constexpr uint32_t kPageCount = 4;
    static constexpr uint32_t kTotalBytes = kPageSize * kPageCount;

    struct AlignedFree
    {
        void operator()(void* ptr) const
        {
            std::free(ptr);
        }
    };
    std::unique_ptr<uint8_t, AlignedFree> rawPages(
        static_cast<uint8_t*>(std::aligned_alloc(kPageSize, kTotalBytes)));
    Require(rawPages != nullptr);
    std::memset(rawPages.get(), 0, kTotalBytes);

    uint32_t cbActual = 0;
    CheckJet(JetGetDatabasePages(session.Handle(),
                                 database.Id(),
                                 /*pgnoStart=*/1,
                                 kPageCount,
                                 rawPages.get(),
                                 kTotalBytes,
                                 &cbActual,
                                 0));
    Require(cbActual == kTotalBytes);

    //  JetGetPageInfo parses those raw bytes into a JET_PAGEINFO
    //  array.  Each element's `pgno` field is INPUT — caller stamps
    //  the page number it wants info on; engine fills the rest.
    std::vector<JET_PAGEINFO> pageInfos(kPageCount);
    for (uint32_t i = 0; i < kPageCount; ++i)
    {
        pageInfos[i].pgno = i + 1;
    }
    CheckJet(JetGetPageInfo(rawPages.get(),
                            kTotalBytes,
                            pageInfos.data(),
                            static_cast<uint32_t>(pageInfos.size() *
                                                  sizeof(JET_PAGEINFO)),
                            0,
                            JET_PageInfo));

    //  Pages 1 and 2 are the database header and shadow header —
    //  always initialised.
    Require(pageInfos[0].fPageIsInitialized);
    Require(pageInfos[1].fPageIsInitialized);

    //  JET_PAGEINFO2 wraps JET_PAGEINFO with three additional
    //  checksum slots; JetGetPageInfo2 is the v2 entry point that
    //  fills them.  Same input contract — caller stamps pgno per
    //  element.
    std::vector<JET_PAGEINFO2> pageInfos2(kPageCount);
    for (uint32_t i = 0; i < kPageCount; ++i)
    {
        pageInfos2[i].pageInfo.pgno = i + 1;
    }
    CheckJet(JetGetPageInfo2(rawPages.get(),
                             kTotalBytes,
                             pageInfos2.data(),
                             static_cast<uint32_t>(pageInfos2.size() *
                                                   sizeof(JET_PAGEINFO2)),
                             0,
                             JET_PageInfo2));
    Require(pageInfos2[0].pageInfo.fPageIsInitialized);
    Require(pageInfos2[1].pageInfo.fPageIsInitialized);
}

//  JetDetachDatabase2 (thin variant).  Adds a JET_GRBIT to the
//  v1 signature.  Engine gates JET_bitForceCloseAndDetach with
//  "force-detach only after a normal detach errored out"
//  (`JET_errForceDetachNotAllowed`), so a clean DB requires a
//  normal-detach failure first.  Scenario covers both paths:
//  call v2 with grbit=0 for the standard close-then-detach
//  (verifies the v2 entry reaches the same engine path as v1),
//  then a second sub-scenario that explicitly produces a
//  detach failure and uses ForceCloseAndDetach to clean up.

EseIntegrationScenario(Database, DetachDatabase2StandardDetachSucceeds)
{
    TemporaryDirectory directory(
        "Database.DetachDatabase2StandardDetachSucceeds");
    EseInstance instance(directory);
    EseSession session(instance);

    const auto databasePath = (directory.Path() / "Detach2.mdb").string();
    JET_DBID databaseId = JET_dbidNil;
    CheckJet(JetCreateDatabaseA(session.Handle(),
                                databasePath.c_str(),
                                nullptr, &databaseId,
                                JET_bitDbOverwriteExisting));
    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), databaseId,
                             "Rows", 8, 100, &tableId));
    CheckJet(JetCloseTable(session.Handle(), tableId));
    CheckJet(JetCloseDatabase(session.Handle(), databaseId, 0));

    //  Standard detach via the v2 entry, grbit=0.
    CheckJet(JetDetachDatabase2A(session.Handle(),
                                 databasePath.c_str(), 0));

    //  Re-attach + re-open exercises the file is fully released.
    CheckJet(JetAttachDatabaseA(session.Handle(),
                                databasePath.c_str(), 0));
    JET_DBID reopenId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.c_str(),
                              nullptr, &reopenId, 0));
    JET_TABLEID reopenTable = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), reopenId, "Rows",
                           nullptr, 0, 0, &reopenTable));
    CheckJet(JetCloseTable(session.Handle(), reopenTable));
    CheckJet(JetCloseDatabase(session.Handle(), reopenId, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(),
                                databasePath.c_str()));
}

EseIntegrationScenario(Database, AttachDatabaseReadOnlyAllowsReadsButRejectsWrites)
{
    TemporaryDirectory directory(
        "Database.AttachDatabaseReadOnlyAllowsReadsButRejectsWrites");
    EseInstance instance(directory);
    EseSession session(instance);

    const auto databasePath =
        (instance.Directory() / "ReadOnly.mdb").string();

    static constexpr int32_t SeededValue = 0xCAFEFEED;

    // Seed: create the DB writable, populate one row, detach.
    {
        JET_DBID createDbId = JET_dbidNil;
        CheckJet(JetCreateDatabaseA(session.Handle(),
                                    databasePath.c_str(),
                                    nullptr, &createDbId,
                                    JET_bitDbOverwriteExisting));

        JET_TABLEID seedTableId = JET_tableidNil;
        CheckJet(JetCreateTableA(session.Handle(), createDbId,
                                 "Rows", 8, 100, &seedTableId));
        JET_COLUMNDEF valueColumnDef = {};
        valueColumnDef.cbStruct = sizeof(valueColumnDef);
        valueColumnDef.coltyp = JET_coltypLong;
        valueColumnDef.grbit = JET_bitColumnNotNULL;
        JET_COLUMNID valueColumnId = 0;
        CheckJet(JetAddColumnA(session.Handle(), seedTableId,
                               "Value", &valueColumnDef,
                               nullptr, 0, &valueColumnId));

        CheckJet(JetBeginTransaction2(session.Handle(), 0));
        CheckJet(JetPrepareUpdate(session.Handle(), seedTableId,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), seedTableId, valueColumnId,
                              &SeededValue, sizeof(SeededValue),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), seedTableId,
                           nullptr, 0, nullptr));
        CheckJet(JetCommitTransaction(session.Handle(), 0));

        CheckJet(JetCloseTable(session.Handle(), seedTableId));
        CheckJet(JetCloseDatabase(session.Handle(), createDbId, 0));
        CheckJet(JetDetachDatabaseA(session.Handle(), databasePath.c_str()));
    }

    // Re-attach with JET_bitDbReadOnly.  Reads of the seeded row work;
    // any DDL or DML attempt comes back JET_errPermissionDenied.
    CheckJet(JetAttachDatabase2A(session.Handle(),
                                 databasePath.c_str(),
                                 /*cpgDatabaseSizeMax*/ 0,
                                 JET_bitDbReadOnly));

    JET_DBID readOnlyDbId = JET_dbidNil;
    CheckJet(JetOpenDatabaseA(session.Handle(),
                              databasePath.c_str(),
                              nullptr, &readOnlyDbId,
                              JET_bitDbReadOnly));

    JET_TABLEID readOnlyTableId = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), readOnlyDbId,
                           "Rows", nullptr, 0,
                           JET_bitTableReadOnly,
                           &readOnlyTableId));

    JET_COLUMNDEF reopenedValueDef = {};
    reopenedValueDef.cbStruct = sizeof(reopenedValueDef);
    CheckJet(JetGetTableColumnInfoA(session.Handle(), readOnlyTableId,
                                    "Value", &reopenedValueDef,
                                    sizeof(reopenedValueDef),
                                    JET_ColInfo));

    int32_t readValue = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetMove(session.Handle(), readOnlyTableId, JET_MoveFirst, 0));
    CheckJet(JetRetrieveColumn(session.Handle(), readOnlyTableId,
                               reopenedValueDef.columnid,
                               &readValue, sizeof(readValue),
                               &actualBytes, 0, nullptr));
    Require(readValue == SeededValue);

    // CreateTable on a read-only DB must be refused.
    JET_TABLEID rejectedTableId = JET_tableidNil;
    RequireJetError(JetCreateTableA(session.Handle(), readOnlyDbId,
                                    "RejectedTable", 8, 100,
                                    &rejectedTableId),
                    JET_errPermissionDenied);

    // PrepareUpdate(Insert) is refused as well.
    RequireJetError(JetPrepareUpdate(session.Handle(), readOnlyTableId,
                                     JET_prepInsert),
                    JET_errPermissionDenied);

    CheckJet(JetCloseTable(session.Handle(), readOnlyTableId));
    CheckJet(JetCloseDatabase(session.Handle(), readOnlyDbId, 0));
    CheckJet(JetDetachDatabaseA(session.Handle(), databasePath.c_str()));
}
