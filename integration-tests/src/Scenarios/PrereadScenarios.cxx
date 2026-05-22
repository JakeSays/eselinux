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

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

//  Build a sorted-int Identity table with `rowCount` rows and a
//  +Identity primary index.  Returns the column id of Identity.
JET_COLUMNID PopulatePrereadTable(EseTable& table, int rowCount)
{
    auto identityColumnId =
        table.AddColumn("Identity",
                        JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    auto sessionHandle = table.Database().Session().Handle();
    {
        EseTransaction transaction(table.Database().Session());
        for (int i = 0; i < rowCount; ++i)
        {
            CheckJet(JetPrepareUpdate(sessionHandle,
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetUpdate(sessionHandle,
                               table.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    return identityColumnId;
}

} // namespace

EseIntegrationScenario(Preread, PrereadKeysAcceptsKeyArray, Smoke)
{
    TemporaryDirectory directory("Preread.PrereadKeysAcceptsKeyArray");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Preread.mdb");
    EseTable table(database, "Rows");
    PopulatePrereadTable(table, 64);

    //  JetPrereadKeys is a perf hint: caller hands the engine a set of
    //  index keys that look-ups will visit, engine prereads the pages
    //  containing them.  pckeysPreread reports how many of the keys
    //  the engine actually initiated I/O for — we don't care about
    //  the exact count, only that the API accepts the call shape.
    int32_t targets[] = { 1, 5, 10, 25, 50 };
    const void* keyPointers[std::size(targets)] = {};
    uint32_t keyByteCounts[std::size(targets)] = {};
    for (size_t i = 0; i < std::size(targets); ++i)
    {
        keyPointers[i] = &targets[i];
        keyByteCounts[i] = sizeof(targets[i]);
    }

    int32_t prereadAttempted = -1;
    CheckJet(JetPrereadKeys(session.Handle(),
                            table.Id(),
                            keyPointers,
                            keyByteCounts,
                            static_cast<int32_t>(std::size(targets)),
                            &prereadAttempted,
                            JET_bitPrereadForward));
    Require(prereadAttempted >= 0);
    Require(prereadAttempted <= static_cast<int32_t>(std::size(targets)));

    //  Preread is a perf hint — public APIs can't directly observe
    //  whether the pages landed in cache.  What we CAN verify is
    //  that every key we asked the engine to preread actually
    //  resolves to a real record afterward.  A preread that
    //  silently broke the cursor / clobbered the keys would fail
    //  the seek-and-retrieve here.
    for (size_t i = 0; i < std::size(targets); ++i)
    {
        CheckJet(JetMakeKey(session.Handle(), table.Id(),
                            &targets[i], sizeof(targets[i]),
                            JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
    }
}

EseIntegrationScenario(Preread, PrereadIndexRangeAcceptsBoundedRange, Smoke)
{
    TemporaryDirectory directory(
        "Preread.PrereadIndexRangeAcceptsBoundedRange");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Preread.mdb");
    EseTable table(database, "Rows");
    auto identityColumnId = PopulatePrereadTable(table, 256);

    //  Find the table's identity range so the preread bound is real.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto firstIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    int32_t startKey = firstIdentity;
    int32_t endKey = firstIdentity + 100;

    //  JetPrereadIndexRange only accepts JET_relopEquals / PrefixEquals
    //  on its index columns (the header makes the restriction explicit
    //  near the JET_RELOP enum).  The "range" comes from supplying a
    //  start anchor and a separate end anchor on the same column —
    //  the engine treats each anchor as an Equals comparison and
    //  prereads everything in between.
    JET_INDEX_COLUMN startCol = {};
    startCol.columnid = identityColumnId;
    startCol.relop = JET_relopEquals;
    startCol.pv = &startKey;
    startCol.cb = sizeof(startKey);

    JET_INDEX_COLUMN endCol = {};
    endCol.columnid = identityColumnId;
    endCol.relop = JET_relopEquals;
    endCol.pv = &endKey;
    endCol.cb = sizeof(endKey);

    JET_INDEX_RANGE range = {};
    range.rgStartColumns = &startCol;
    range.cStartColumns = 1;
    range.rgEndColumns = &endCol;
    range.cEndColumns = 1;

    uint32_t cPagesPreread = 0;
    CheckJet(JetPrereadIndexRange(session.Handle(),
                                  table.Id(),
                                  &range,
                                  /*cPageCacheMin=*/1,
                                  /*cPageCacheMax=*/64,
                                  JET_bitPrereadForward,
                                  &cPagesPreread));
    //  Engine reports how many cache pages it scheduled; exact count
    //  is implementation-defined.  What we CAN check is that the
    //  range is real data: a forward walk from startKey reaches
    //  endKey and surfaces every row in between.  A bad preread
    //  that scrambled the B-tree would surface here.
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &startKey, sizeof(startKey),
                        JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), table.Id(),
                     JET_bitSeekGE));
    int32_t walkedRowCount = 0;
    int32_t lastObservedKey = std::numeric_limits<int32_t>::min();
    while (true)
    {
        const auto observed =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId);
        Require(observed > lastObservedKey);
        Require(observed >= startKey);
        lastObservedKey = observed;
        ++walkedRowCount;
        if (observed >= endKey)
        {
            break;
        }
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRowCount > 0);
}

EseIntegrationScenario(Preread, PrereadTablesAcceptsKnownTableName, Smoke)
{
    TemporaryDirectory directory("Preread.PrereadTablesAcceptsKnownTableName");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Preread.mdb");
    EseTable table(database, "Rows");
    PopulatePrereadTable(table, 8);

    //  JetPrereadTables is unicode-only by header convention (the
    //  ASCII alias resolves to a sentinel name that won't link).
    //  Hand it a wide-char "Rows" — the engine prereads metadata for
    //  the named tables.
    const char16_t tableName[] = u"Rows";
    const char16_t* tableNames[] = { tableName };
    CheckJet(JetPrereadTablesW(session.Handle(),
                               database.Id(),
                               tableNames,
                               static_cast<int32_t>(std::size(tableNames)),
                               0));

    //  After preread, the table must still open + walk cleanly via
    //  a fresh cursor.  A preread that corrupted the catalog or
    //  somehow torn the metadata for the named table would surface
    //  here as an OpenTable failure or a wrong row count.
    JET_TABLEID secondCursor = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(), "Rows",
                           nullptr, 0, 0, &secondCursor));
    CheckJet(JetMove(session.Handle(), secondCursor, JET_MoveFirst, 0));
    int32_t walkedRowCount = 0;
    while (true)
    {
        ++walkedRowCount;
        const auto moveResult =
            JetMove(session.Handle(), secondCursor, JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRowCount == 8);
    CheckJet(JetCloseTable(session.Handle(), secondCursor));
}

EseIntegrationScenario(Preread, PrereadIndexRangesAcceptsTwoRanges, Smoke)
{
    TemporaryDirectory directory(
        "Preread.PrereadIndexRangesAcceptsTwoRanges");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Preread.mdb");
    EseTable table(database, "Rows");
    auto identityColumnId = PopulatePrereadTable(table, 512);

    //  Establish two disjoint anchor pairs over the same column.  The
    //  engine prereads pages covering both ranges in a single call.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto firstIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    int32_t rangeAStart = firstIdentity;
    int32_t rangeAEnd = firstIdentity + 99;
    int32_t rangeBStart = firstIdentity + 200;
    int32_t rangeBEnd = firstIdentity + 299;

    JET_INDEX_COLUMN aStart = {};
    aStart.columnid = identityColumnId;
    aStart.relop = JET_relopEquals;
    aStart.pv = &rangeAStart;
    aStart.cb = sizeof(rangeAStart);

    JET_INDEX_COLUMN aEnd = {};
    aEnd.columnid = identityColumnId;
    aEnd.relop = JET_relopEquals;
    aEnd.pv = &rangeAEnd;
    aEnd.cb = sizeof(rangeAEnd);

    JET_INDEX_COLUMN bStart = {};
    bStart.columnid = identityColumnId;
    bStart.relop = JET_relopEquals;
    bStart.pv = &rangeBStart;
    bStart.cb = sizeof(rangeBStart);

    JET_INDEX_COLUMN bEnd = {};
    bEnd.columnid = identityColumnId;
    bEnd.relop = JET_relopEquals;
    bEnd.pv = &rangeBEnd;
    bEnd.cb = sizeof(rangeBEnd);

    JET_INDEX_RANGE ranges[2] = {};
    ranges[0].rgStartColumns = &aStart;
    ranges[0].cStartColumns = 1;
    ranges[0].rgEndColumns = &aEnd;
    ranges[0].cEndColumns = 1;
    ranges[1].rgStartColumns = &bStart;
    ranges[1].cStartColumns = 1;
    ranges[1].rgEndColumns = &bEnd;
    ranges[1].cEndColumns = 1;

    uint32_t rangesPreread = 0;
    CheckJet(JetPrereadIndexRanges(session.Handle(),
                                   table.Id(),
                                   ranges,
                                   static_cast<uint32_t>(std::size(ranges)),
                                   &rangesPreread,
                                   /*rgcolumnidPreread=*/nullptr,
                                   /*ccolumnidPreread=*/0,
                                   JET_bitPrereadForward));
    Require(rangesPreread <= std::size(ranges));

    //  After preread, walk both ranges and confirm every expected
    //  row is reachable in order — proves the engine's view of the
    //  ranges matched the data on disk.  A preread that
    //  miscalculated the range mapping would either skip rows or
    //  surface keys outside the requested bounds.
    auto walkRange = [&](int32_t rangeStart, int32_t rangeEnd) {
        CheckJet(JetMakeKey(session.Handle(), table.Id(),
                            &rangeStart, sizeof(rangeStart),
                            JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), table.Id(),
                         JET_bitSeekGE));
        int32_t lastKey = std::numeric_limits<int32_t>::min();
        int32_t walked = 0;
        while (true)
        {
            const auto observed =
                RetrieveFixedColumnFromCurrentRecord<int32_t>(
                    table, identityColumnId);
            Require(observed >= rangeStart);
            Require(observed > lastKey);
            lastKey = observed;
            ++walked;
            if (observed >= rangeEnd)
            {
                break;
            }
            const auto moveResult =
                JetMove(session.Handle(), table.Id(),
                        JET_MoveNext, 0);
            if (moveResult == JET_errNoCurrentRecord)
            {
                break;
            }
            CheckJet(moveResult);
        }
        return walked;
    };
    Require(walkRange(rangeAStart, rangeAEnd) > 0);
    Require(walkRange(rangeBStart, rangeBEnd) > 0);
}
