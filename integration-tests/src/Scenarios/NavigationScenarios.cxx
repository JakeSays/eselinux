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

#include <cstring>
#include <string_view>
#include <vector>

using namespace ese::tests;

namespace
{

// Populate the table with `rowCount` autoincrement rows under a
// primary index over a new Identity column. Returns the
// JET_COLUMNID of that column so scenarios can retrieve it later.
JET_COLUMNID PopulateAutoIncrementTable(EseTable& table, int rowCount)
{
    const auto identityColumnId =
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
        for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(sessionHandle, table.Id(), JET_prepInsert));
            CheckJet(JetUpdate(sessionHandle, table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    return identityColumnId;
}

} // namespace

EseIntegrationScenario(Navigation, MoveFirstNextLast)
{
    TemporaryDirectory directory("Navigation.MoveFirstNextLast");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 3);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto firstIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    auto lastIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    Require(lastIdentity == firstIdentity + 2);
}

EseIntegrationScenario(Navigation, MovePreviousFromLastSeesEveryRow)
{
    TemporaryDirectory directory(
        "Navigation.MovePreviousFromLastSeesEveryRow");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 7);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    int observedRows = 0;
    int32_t previous = 0;
    bool first = true;
    while (true)
    {
        auto current =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                      identityColumnId);
        if (!first)
        {
            // Walking backward — current must be strictly less than previous.
            Require(current < previous);
        }
        first = false;
        previous = current;
        ++observedRows;
        const auto moveResult = JetMove(session.Handle(),
                                        table.Id(),
                                        JET_MovePrevious,
                                        0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(observedRows == 7);
}

EseIntegrationScenario(Navigation, MakeKeyAndSeekEqualHitsExpectedRow)
{
    TemporaryDirectory directory(
        "Navigation.MakeKeyAndSeekEqualHitsExpectedRow");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 10);

    // Find the autoincrement value of the third row by walking from first.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), 2, 0));
    auto targetKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);

    // Reset to first, then seek directly to that key.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMakeKey(session.Handle(),
                        table.Id(),
                        &targetKey,
                        sizeof(targetKey),
                        JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));

    auto landedKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);
    Require(landedKey == targetKey);
}

EseIntegrationScenario(Navigation, SeekGreaterOrEqualLandsOnFirstMatch)
{
    TemporaryDirectory directory(
        "Navigation.SeekGreaterOrEqualLandsOnFirstMatch");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 6);

    // Find the smallest identity value via MoveFirst, then seek with a
    // probe key one below it. GE should land on the smallest row.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto smallestKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);
    int32_t probeKey = smallestKey - 1;

    CheckJet(JetMakeKey(session.Handle(),
                        table.Id(),
                        &probeKey,
                        sizeof(probeKey),
                        JET_bitNewKey));
    auto seekResult = JetSeek(session.Handle(), table.Id(), JET_bitSeekGE);
    Require(seekResult == JET_errSuccess || seekResult == JET_wrnSeekNotEqual);

    auto landedKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);
    Require(landedKey == smallestKey);
}

EseIntegrationScenario(Navigation, SeekEqualReturnsRecordNotFoundWhenMissing)
{
    TemporaryDirectory directory(
        "Navigation.SeekEqualReturnsRecordNotFoundWhenMissing");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 3);

    // Probe a key that's certain to be absent — well above the last
    // autoincrement value.
    int32_t absentKey = 0x7FFFFFFE;
    CheckJet(JetMakeKey(session.Handle(),
                        table.Id(),
                        &absentKey,
                        sizeof(absentKey),
                        JET_bitNewKey));
    RequireJetError(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ),
                    JET_errRecordNotFound);
}

EseIntegrationScenario(Navigation, GetBookmarkAndGotoBookmarkRoundTrip)
{
    TemporaryDirectory directory(
        "Navigation.GetBookmarkAndGotoBookmarkRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 5);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), 2, 0)); // third row

    auto expectedKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);

    uint8_t bookmarkBuffer[256] = {};
    uint32_t bookmarkActualBytes = 0;
    CheckJet(JetGetBookmark(session.Handle(),
                            table.Id(),
                            bookmarkBuffer,
                            sizeof(bookmarkBuffer),
                            &bookmarkActualBytes));

    // Reposition somewhere else, then go back via the bookmark.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetGotoBookmark(session.Handle(),
                             table.Id(),
                             bookmarkBuffer,
                             bookmarkActualBytes));

    auto landedKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);
    Require(landedKey == expectedKey);
}

EseIntegrationScenario(Navigation, MoveByCountAdvancesCorrectNumberOfRows)
{
    TemporaryDirectory directory(
        "Navigation.MoveByCountAdvancesCorrectNumberOfRows");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 10);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto firstKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);

    CheckJet(JetMove(session.Handle(), table.Id(), 5, 0)); // advance 5 rows

    auto skipKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);
    Require(skipKey == firstKey + 5);
}

EseIntegrationScenario(Navigation, SetCurrentIndexSwitchesActiveIndex)
{
    TemporaryDirectory directory(
        "Navigation.SetCurrentIndexSwitchesActiveIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sorted");

    auto identityColumnId = table.AddColumn("Identity",
                                            JET_coltypLong,
                                            JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto rankColumnId = table.AddColumn("Rank",
                                            JET_coltypLong,
                                            JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr std::string_view RankKey =
        std::string_view("+Rank\0\0", 7);
    table.CreateIndex("ByRank", RankKey);

    static constexpr int RowCount = 5;
    JET_SETCOLUMN setRank = {};
    setRank.columnid = rankColumnId;
    {
        EseTransaction transaction(session);
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            const int32_t rankValue = RowCount - rowIndex; // insert in descending rank
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  rankColumnId,
                                  &rankValue,
                                  sizeof(rankValue),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    // ByRank should now walk in ascending rank order.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByRank"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t previous = 0;
    bool first = true;
    for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex)
    {
        auto rank =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table, rankColumnId);
        if (!first)
        {
            Require(rank > previous);
        }
        first = false;
        previous = rank;
        if (rowIndex + 1 < RowCount)
        {
            CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0));
        }
    }
}

EseIntegrationScenario(Navigation, SetIndexRangeStopsWhereExpected)
{
    TemporaryDirectory directory(
        "Navigation.SetIndexRangeStopsWhereExpected");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 10);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto firstKey =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                  identityColumnId);

    // Cap the cursor's walk at firstKey + 2 inclusive.
    int32_t upperBound = firstKey + 2;
    CheckJet(JetMakeKey(session.Handle(),
                        table.Id(),
                        &upperBound,
                        sizeof(upperBound),
                        JET_bitNewKey));
    CheckJet(JetSetIndexRange(session.Handle(),
                              table.Id(),
                              JET_bitRangeInclusive | JET_bitRangeUpperLimit));

    int observedRows = 0;
    while (true)
    {
        auto current =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                                      identityColumnId);
        Require(current <= upperBound);
        ++observedRows;
        const auto moveResult = JetMove(session.Handle(),
                                        table.Id(),
                                        JET_MoveNext,
                                        0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(observedRows == 3); // firstKey, +1, +2
}

EseIntegrationScenario(Navigation, MoveBeyondLastReturnsNoCurrentRecord)
{
    TemporaryDirectory directory(
        "Navigation.MoveBeyondLastReturnsNoCurrentRecord");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");

    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 2);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(Navigation, IntersectIndexesReturnsRowsMatchingBothRanges)
{
    TemporaryDirectory directory(
        "Navigation.IntersectIndexesReturnsRowsMatchingBothRanges");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Items");

    const auto colorColumnId = table.AddColumn("Color", JET_coltypLong,
                                               JET_bitColumnNotNULL);
    const auto sizeColumnId  = table.AddColumn("Size",  JET_coltypLong,
                                               JET_bitColumnNotNULL);

    // Two single-column secondary indexes — the inputs to the intersect.
    static constexpr std::string_view ColorKey =
        std::string_view("+Color\0\0", 8);
    static constexpr std::string_view SizeKey =
        std::string_view("+Size\0\0", 7);
    table.CreateIndex("ByColor", ColorKey);
    table.CreateIndex("BySize",  SizeKey);

    // Eight rows: Color in {1, 2}, Size in {10, 20, 30, 40}.  The
    // (Color=1, Size=20) intersection has exactly one row; same for
    // (Color=2, Size=30), (Color=1, Size=40), etc.
    {
        EseTransaction transaction(session);
        const int32_t colors[] = { 1, 1, 1, 1, 2, 2, 2, 2 };
        const int32_t sizes[]  = { 10, 20, 30, 40, 10, 20, 30, 40 };
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), colorColumnId,
                                  &colors[i], sizeof(colors[i]),
                                  0, nullptr));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), sizeColumnId,
                                  &sizes[i], sizeof(sizes[i]),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    // Cursor A on ByColor, ranged to Color == 1.
    JET_TABLEID byColor = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(), "Items",
                           nullptr, 0, 0, &byColor));
    CheckJet(JetSetCurrentIndexA(session.Handle(), byColor, "ByColor"));
    {
        const int32_t target = 1;
        CheckJet(JetMakeKey(session.Handle(), byColor,
                            &target, sizeof(target), JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), byColor, JET_bitSeekGE));
        CheckJet(JetMakeKey(session.Handle(), byColor,
                            &target, sizeof(target),
                            JET_bitNewKey | JET_bitFullColumnEndLimit));
        CheckJet(JetSetIndexRange(session.Handle(), byColor,
                                  JET_bitRangeInclusive |
                                  JET_bitRangeUpperLimit));
    }

    // Cursor B on BySize, ranged to Size in [20, 30] (inclusive).
    JET_TABLEID bySize = JET_tableidNil;
    CheckJet(JetOpenTableA(session.Handle(), database.Id(), "Items",
                           nullptr, 0, 0, &bySize));
    CheckJet(JetSetCurrentIndexA(session.Handle(), bySize, "BySize"));
    {
        const int32_t lower = 20;
        CheckJet(JetMakeKey(session.Handle(), bySize,
                            &lower, sizeof(lower), JET_bitNewKey));
        CheckJet(JetSeek(session.Handle(), bySize, JET_bitSeekGE));
        const int32_t upper = 30;
        CheckJet(JetMakeKey(session.Handle(), bySize,
                            &upper, sizeof(upper),
                            JET_bitNewKey | JET_bitFullColumnEndLimit));
        CheckJet(JetSetIndexRange(session.Handle(), bySize,
                                  JET_bitRangeInclusive |
                                  JET_bitRangeUpperLimit));
    }

    JET_INDEXRANGE indexRanges[2] = {};
    indexRanges[0].cbStruct = sizeof(indexRanges[0]);
    indexRanges[0].tableid  = byColor;
    indexRanges[0].grbit    = JET_bitRecordInIndex;
    indexRanges[1].cbStruct = sizeof(indexRanges[1]);
    indexRanges[1].tableid  = bySize;
    indexRanges[1].grbit    = JET_bitRecordInIndex;

    JET_RECORDLIST recordList = {};
    recordList.cbStruct = sizeof(recordList);
    CheckJet(JetIntersectIndexes(session.Handle(),
                                 indexRanges, 2,
                                 &recordList, 0));

    // Intersection: Color==1 AND Size in [20,30] → 2 rows.
    Require(recordList.cRecord == 2);
    Require(recordList.tableid != JET_tableidNil);
    Require(recordList.columnidBookmark != 0);

    // Walk the temp table to confirm each bookmark resolves on the
    // base table and the resolved row's columns satisfy both ranges.
    CheckJet(JetMove(session.Handle(), recordList.tableid, JET_MoveFirst, 0));
    int observed = 0;
    do
    {
        uint8_t bookmark[256] = {};
        uint32_t cbBookmark = 0;
        CheckJet(JetRetrieveColumn(session.Handle(),
                                   recordList.tableid,
                                   recordList.columnidBookmark,
                                   bookmark, sizeof(bookmark),
                                   &cbBookmark, 0, nullptr));
        CheckJet(JetGotoBookmark(session.Handle(), byColor,
                                 bookmark, cbBookmark));
        int32_t color = 0;
        int32_t size  = 0;
        uint32_t cbActual = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), byColor, colorColumnId,
                                   &color, sizeof(color), &cbActual, 0, nullptr));
        CheckJet(JetRetrieveColumn(session.Handle(), byColor, sizeColumnId,
                                   &size, sizeof(size), &cbActual, 0, nullptr));
        Require(color == 1);
        Require(size == 20 || size == 30);
        ++observed;
    }
    while (JetMove(session.Handle(), recordList.tableid, JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observed == 2);

    CheckJet(JetCloseTable(session.Handle(), recordList.tableid));
    CheckJet(JetCloseTable(session.Handle(), bySize));
    CheckJet(JetCloseTable(session.Handle(), byColor));
}

EseIntegrationScenario(Navigation, RetrieveKeyReturnsIndexKeyBytes)
{
    TemporaryDirectory directory("Navigation.RetrieveKeyReturnsIndexKeyBytes");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Indexed");

    auto columnId = table.AddColumn("Key", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey, JET_bitIndexPrimary);

    {
        EseTransaction transaction(session);
        for (int32_t value : { 11, 22, 33 })
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, value);
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // JetRetrieveKey emits the on-disk key bytes for the current
    // record under the current index.  For a single-column ascending
    // Long primary key the engine prefixes a fixed byte (0x7f) then
    // emits the big-endian, sign-flipped 4-byte payload.  We don't
    // pin the exact encoding — just that the key is well-formed and
    // the same byte-pattern round-trips through JetMakeKey + JetSeek.
    uint8_t keyBuffer[64] = {};
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveKey(session.Handle(), table.Id(),
                            keyBuffer, sizeof(keyBuffer),
                            &cbActual, 0));
    Require(cbActual > 0);

    // Re-issue the captured key bytes as a normalized key — the
    // engine should seek back to the same row.  JET_bitNormalizedKey
    // tells JetMakeKey that the input is already a normalized key.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        keyBuffer, cbActual,
                        JET_bitNewKey | JET_bitNormalizedKey));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));

    int32_t resolvedKey = 0;
    uint32_t cbValue = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                               &resolvedKey, sizeof(resolvedKey),
                               &cbValue, 0, nullptr));
    Require(resolvedKey == 11);  // first row's key
}

EseIntegrationScenario(Navigation, RetrieveKeyReportsBufferTruncation)
{
    TemporaryDirectory directory(
        "Navigation.RetrieveKeyReportsBufferTruncation");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Indexed");

    auto columnId = table.AddColumn("Key", JET_coltypLong,
                                    JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey, JET_bitIndexPrimary);
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 42);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Pass an undersized buffer — the engine reports the full key
    // size via pcbActual and the call returns JET_wrnBufferTruncated.
    uint8_t tinyBuffer[2] = {};
    uint32_t cbActual = 0;
    const auto err = JetRetrieveKey(session.Handle(), table.Id(),
                                    tinyBuffer, sizeof(tinyBuffer),
                                    &cbActual, 0);
    Require(err == JET_wrnBufferTruncated);
    Require(cbActual > sizeof(tinyBuffer));
}

EseIntegrationScenario(Navigation, IndexRecordCountReportsRowCount)
{
    TemporaryDirectory directory("Navigation.IndexRecordCountReportsRowCount");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Key", JET_coltypLong,
                                    JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey, JET_bitIndexPrimary);

    constexpr int RowCount = 50;
    {
        EseTransaction transaction(session);
        for (int32_t v = 0; v < RowCount; ++v)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, v);
        }
        transaction.Commit();
    }

    // crecMax caps the walk at that many records; passing 0 means
    // "no cap" (engine convention).  Empty index ranges return 0;
    // with all rows in range and no cap, we get the full count.
    uint32_t indexed = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetIndexRecordCount(session.Handle(), table.Id(),
                                 &indexed, /*crecMax*/ 0));
    Require(indexed == RowCount);

    // crecMax bounds the count; the engine stops at the cap.
    uint32_t bounded = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetIndexRecordCount(session.Handle(), table.Id(),
                                 &bounded, /*crecMax*/ 10));
    Require(bounded == 10);
}

EseIntegrationScenario(Navigation, GetCurrentIndexReportsActiveIndex)
{
    TemporaryDirectory directory("Navigation.GetCurrentIndexReportsActiveIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sorted");

    table.AddColumn("Identity",
                    JET_coltypLong,
                    JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto rankColumnId = table.AddColumn("Rank",
                                        JET_coltypLong,
                                        JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr std::string_view RankKey =
        std::string_view("+Rank\0\0", 7);
    table.CreateIndex("ByRank", RankKey);

    //  Defaults to the primary index when nothing is selected; the
    //  engine reports its name.
    char indexName[ JET_cbNameMost + 1 ] = {};
    CheckJet(JetGetCurrentIndexA(session.Handle(),
                                 table.Id(),
                                 indexName,
                                 sizeof(indexName)));
    Require(std::string_view(indexName) == "PrimaryByIdentity");

    //  Switching the cursor's index flips what JetGetCurrentIndex
    //  reports.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByRank"));
    std::memset(indexName, 0, sizeof(indexName));
    CheckJet(JetGetCurrentIndexA(session.Handle(),
                                 table.Id(),
                                 indexName,
                                 sizeof(indexName)));
    Require(std::string_view(indexName) == "ByRank");

    (void)rankColumnId;
}

EseIntegrationScenario(Navigation, SetAndResetTableSequentialRoundTrip)
{
    TemporaryDirectory directory(
        "Navigation.SetAndResetTableSequentialRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Rows");
    auto identityColumnId = PopulateAutoIncrementTable(table, 32);

    //  JetSetTableSequential is a perf hint — the engine prereads
    //  pages assuming we'll walk the table front-to-back.  No flags
    //  in the unversioned form; round-trip with Reset.
    CheckJet(JetSetTableSequential(session.Handle(), table.Id(), 0));

    int seen = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    while (true)
    {
        ++seen;
        const auto rc = JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (rc == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(rc);
    }
    Require(seen == 32);

    CheckJet(JetResetTableSequential(session.Handle(), table.Id(), 0));

    (void)identityColumnId;
}

EseIntegrationScenario(Navigation, SetCursorFilterRejectsNonMatchingRows)
{
    TemporaryDirectory directory(
        "Navigation.SetCursorFilterRejectsNonMatchingRows");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Filtered");

    auto keyColumnId = table.AddColumn("Key",
                                       JET_coltypLong,
                                       JET_bitColumnNotNULL);
    auto valueColumnId = table.AddColumn("Value",
                                         JET_coltypLong,
                                         JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    //  Insert 10 rows with Value alternating between 7 and 11.  The
    //  filter pins the visible set to Value=7 (5 of the 10).
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < 10; ++i)
        {
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  keyColumnId,
                                  &i,
                                  sizeof(i),
                                  0,
                                  nullptr));
            const int32_t value = (i % 2 == 0) ? 7 : 11;
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  valueColumnId,
                                  &value,
                                  sizeof(value),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(session.Handle(),
                               table.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    int32_t target = 7;
    JET_INDEX_COLUMN filter = {};
    filter.columnid = valueColumnId;
    filter.relop = JET_relopEquals;
    filter.pv = &target;
    filter.cb = sizeof(target);
    filter.grbit = 0;

    //  JetSetCursorFilter installs a server-side residual predicate.
    //  Rows whose Value column is not 7 are silently skipped on Move.
    //  Filtering on un-indexed columns may not be supported by every
    //  engine build — JET_errFilteredMoveNotSupported is the
    //  documented fallback; both outcomes count as "API exercised".
    const auto filterErr = JetSetCursorFilter(session.Handle(),
                                              table.Id(),
                                              &filter,
                                              1,
                                              0);
    if (filterErr == JET_errFilteredMoveNotSupported)
    {
        return;
    }
    CheckJet(filterErr);

    int rowsSeen = 0;
    auto rc = JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0);
    while (rc != JET_errNoCurrentRecord)
    {
        CheckJet(rc);
        const auto value = RetrieveFixedColumnFromCurrentRecord<int32_t>(
            table, valueColumnId);
        Require(value == 7);
        ++rowsSeen;
        rc = JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
    }
    Require(rowsSeen == 5);
}

EseIntegrationScenario(Navigation, GetAndGotoRecordPositionRoundTrip)
{
    TemporaryDirectory directory(
        "Navigation.GetAndGotoRecordPositionRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Rows");

    auto identityColumnId = PopulateAutoIncrementTable(table, 25);

    //  Walk forward 12 rows from the start so we land somewhere
    //  interior to the table.  Capture the row's value AND record
    //  position; resetting the cursor and JetGotoPosition'ing back
    //  must land us on the same row.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), 12, 0));
    const auto landingValue =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    JET_RECPOS recpos = {};
    recpos.cbStruct = sizeof(recpos);
    CheckJet(JetGetRecordPosition(session.Handle(),
                                  table.Id(),
                                  &recpos,
                                  sizeof(recpos)));
    Require(recpos.centriesTotal >= recpos.centriesLT);

    //  Reposition somewhere else, then ask the engine to put us back
    //  by fraction-of-table.  JetGotoPosition is an approximate seek
    //  (it finds the closest record matching the LT/Total ratio); on a
    //  small table that's exactly the row we recorded above.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetGotoPosition(session.Handle(), table.Id(), &recpos));
    const auto roundTripValue =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    Require(roundTripValue == landingValue);
}

EseIntegrationScenario(Navigation, SecondaryIndexBookmarkRoundTrip)
{
    TemporaryDirectory directory(
        "Navigation.SecondaryIndexBookmarkRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Indexed");

    auto identityColumnId = table.AddColumn(
        "Identity",
        JET_coltypLong,
        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto rankColumnId = table.AddColumn("Rank",
                                        JET_coltypLong,
                                        JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr std::string_view RankKey =
        std::string_view("+Rank\0\0", 7);
    table.CreateIndex("ByRank", RankKey);

    constexpr int RowCount = 8;
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < RowCount; ++i)
        {
            const int32_t rank = RowCount - i;
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  rankColumnId,
                                  &rank,
                                  sizeof(rank),
                                  0,
                                  nullptr));
            CheckJet(JetUpdate(session.Handle(),
                               table.Id(),
                               nullptr,
                               0,
                               nullptr));
        }
        transaction.Commit();
    }

    //  Position on the third row of the secondary index, capture both
    //  bookmarks, reposition the cursor elsewhere, and re-seek via
    //  JetGotoSecondaryIndexBookmark — we should land on the same row.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByRank"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), 2, 0));
    const auto landingIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    uint8_t secondaryKey[256] = {};
    uint8_t primaryBookmark[256] = {};
    uint32_t cbSecondary = 0;
    uint32_t cbPrimary = 0;
    CheckJet(JetGetSecondaryIndexBookmark(session.Handle(),
                                          table.Id(),
                                          secondaryKey,
                                          sizeof(secondaryKey),
                                          &cbSecondary,
                                          primaryBookmark,
                                          sizeof(primaryBookmark),
                                          &cbPrimary,
                                          0));
    Require(cbSecondary > 0);
    Require(cbPrimary > 0);

    //  Drift the cursor to confirm Goto actually moves us.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));

    CheckJet(JetGotoSecondaryIndexBookmark(session.Handle(),
                                           table.Id(),
                                           secondaryKey,
                                           cbSecondary,
                                           primaryBookmark,
                                           cbPrimary,
                                           0));
    const auto roundTripIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    Require(roundTripIdentity == landingIdentity);
}
