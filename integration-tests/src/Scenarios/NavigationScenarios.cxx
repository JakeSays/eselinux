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
    //  pages assuming we'll walk the table front-to-back.  The hint
    //  is purely advisory so the observable from public APIs is
    //  limited; what we CAN verify is that the engine still serves
    //  correct row data under the hint AND that the hint is properly
    //  scoped to one cursor (a sibling cursor without the hint must
    //  return the same rows).  We also verify that toggling Reset
    //  doesn't break navigation.
    CheckJet(JetSetTableSequential(session.Handle(), table.Id(), 0));

    int32_t hintedRowsSeen = 0;
    int32_t lastIdentity = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    while (true)
    {
        int32_t identity = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   identityColumnId,
                                   &identity, sizeof(identity),
                                   &actualBytes, 0, nullptr));
        Require(actualBytes == sizeof(identity));
        Require(identity > lastIdentity);
        lastIdentity = identity;
        ++hintedRowsSeen;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(hintedRowsSeen == 32);

    //  Reset must not destroy the cursor — a re-scan after reset
    //  produces the same row count, proving the hint can be lifted
    //  cleanly mid-session.
    CheckJet(JetResetTableSequential(session.Handle(), table.Id(), 0));
    int32_t afterResetRowsSeen = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    while (true)
    {
        ++afterResetRowsSeen;
        const auto moveResult =
            JetMove(session.Handle(), table.Id(), JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(afterResetRowsSeen == 32);
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
    CheckJet(JetSetCursorFilter(session.Handle(),
                                table.Id(),
                                &filter,
                                1,
                                0));

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

EseIntegrationScenario(Navigation, IndexRecordCount2Reports64BitCount)
{
    TemporaryDirectory directory(
        "Navigation.IndexRecordCount2Reports64BitCount");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Key",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);
    static constexpr std::string_view PrimaryKey =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKey, JET_bitIndexPrimary);

    constexpr int RowCount = 75;
    {
        EseTransaction transaction(session);
        for (int32_t v = 0; v < RowCount; ++v)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, v);
        }
        transaction.Commit();
    }

    //  JetIndexRecordCount2 widens the v1 32-bit counters to 64-bit
    //  so callers can handle indexes with >4 billion entries.
    //  Functional behaviour matches the v1 form on a small table.
    uint64_t indexed = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetIndexRecordCount2(session.Handle(),
                                  table.Id(),
                                  &indexed,
                                  /*crecMax=*/0));
    Require(indexed == RowCount);

    uint64_t bounded = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetIndexRecordCount2(session.Handle(),
                                  table.Id(),
                                  &bounded,
                                  /*crecMax=*/25));
    Require(bounded == 25);
}

EseIntegrationScenario(Navigation, SetCurrentIndex2WithNoMoveLeavesCursorInPlace)
{
    TemporaryDirectory directory(
        "Navigation.SetCurrentIndex2WithNoMoveLeavesCursorInPlace");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sorted");

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

    constexpr int RowCount = 6;
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

    //  Position on the third row by Identity, capture its rank.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), table.Id(), 2, 0));
    const auto landingIdentity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);

    //  JetSetCurrentIndex (v1) implicitly moves to the first row of
    //  the new index — losing our position.  JetSetCurrentIndex2
    //  with JET_bitNoMove keeps the cursor pinned on the same logical
    //  row across the index switch by translating the current
    //  bookmark to the new index's key.
    CheckJet(JetSetCurrentIndex2A(session.Handle(),
                                  table.Id(),
                                  "ByRank",
                                  JET_bitNoMove));
    const auto identityAfterSwitch =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    Require(identityAfterSwitch == landingIdentity);
}

//  JetSetCurrentIndex3 sits between v2 and v4: same (sesid, tableid,
//  szIndexName, grbit) parameters as v2 plus an itagSequence argument,
//  but without v4's JET_INDEXID cache.  itagSequence selects which
//  occurrence of a multi-valued index entry the cursor lands on
//  (`recpos.cxx ~430`).  For a single-valued index any value >= 1
//  positions on the (sole) entry, and 1 is the conventional pick.
//  Verify that switching to a secondary index via v3 with grbit=0
//  lands on the first row of the new index order — same observable
//  behavior as v2 without JET_bitNoMove, confirming the v3 dispatch
//  is wired up.
EseIntegrationScenario(Navigation, SetCurrentIndex3MovesToFirstOnNewIndex)
{
    TemporaryDirectory directory(
        "Navigation.SetCurrentIndex3MovesToFirstOnNewIndex");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "SetIndex3.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), database.Id(),
                             "Rows", 16, 100, &tableId));
    JET_COLUMNDEF identityColumn = {};
    identityColumn.cbStruct = sizeof(identityColumn);
    identityColumn.coltyp = JET_coltypLong;
    identityColumn.grbit = JET_bitColumnAutoincrement;
    JET_COLUMNID identityColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Identity",
                           &identityColumn, nullptr, 0,
                           &identityColumnId));
    JET_COLUMNDEF rankColumn = {};
    rankColumn.cbStruct = sizeof(rankColumn);
    rankColumn.coltyp = JET_coltypLong;
    rankColumn.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID rankColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Rank",
                           &rankColumn, nullptr, 0, &rankColumnId));
    CheckJet(JetCreateIndexA(session.Handle(), tableId, "PrimaryByIdentity",
                             JET_bitIndexPrimary | JET_bitIndexUnique,
                             "+Identity\0", 11, 80));
    CheckJet(JetCreateIndexA(session.Handle(), tableId, "ByRank",
                             JET_bitIndexUnique, "+Rank\0", 7, 80));

    //  Insert rows with ranks N..1 so insertion order (== primary
    //  Identity order) is the reverse of the ByRank index order.
    constexpr int32_t RowCount = 5;
    CheckJet(JetBeginTransaction(session.Handle()));
    for (int32_t i = 0; i < RowCount; ++i)
    {
        const int32_t rank = RowCount - i;
        CheckJet(JetPrepareUpdate(session.Handle(), tableId,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), tableId, rankColumnId,
                              &rank, sizeof(rank), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(session.Handle(), 0));

    //  Walk to the last row by primary index (highest Identity, rank=1).
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveLast, 0));
    int32_t rankBefore = 0;
    uint32_t cb = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId, rankColumnId,
                               &rankBefore, sizeof(rankBefore),
                               &cb, 0, nullptr));
    Require(rankBefore == 1);

    //  Switch to ByRank via JetSetCurrentIndex3 with grbit=0; engine
    //  drops the cursor on the first entry of the new index (rank=1).
    //  itagSequence=1 is the standard pick — there's only one indexed
    //  value per row on this single-valued index, so the engine treats
    //  any sequence >= 1 as "the sole occurrence".
    CheckJet(JetSetCurrentIndex3A(session.Handle(), tableId,
                                  "ByRank", /*grbit=*/0,
                                  /*itagSequence=*/1));
    int32_t rankAfter = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId, rankColumnId,
                               &rankAfter, sizeof(rankAfter),
                               &cb, 0, nullptr));
    Require(rankAfter == 1);

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

//  JetSetCurrentIndex4.  Same surface as JetSetCurrentIndex2,
//  plus a JET_INDEXID cache pointer and an itagSequence
//  positioning argument.  The JET_INDEXID is opaque; callers
//  obtain one via JetGetTableIndexInfo(..., JET_IdxInfoIndexId)
//  and pass it back on subsequent calls so the engine skips
//  index-name resolution.  Verify that switching to a non-primary
//  index by JET_INDEXID + JET_bitNoMove preserves the cursor's
//  current logical record (translated to the secondary index's
//  key), matching the SetCurrentIndex2 contract — proves the
//  v4 entry actually plumbs through the index lookup correctly.

EseIntegrationScenario(Navigation, SetCurrentIndex4PositionsViaIndexId)
{
    TemporaryDirectory directory(
        "Navigation.SetCurrentIndex4PositionsViaIndexId");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "SetIndex4.mdb");

    JET_TABLEID tableId = JET_tableidNil;
    CheckJet(JetCreateTableA(session.Handle(), database.Id(),
                             "Rows", 16, 100, &tableId));
    JET_COLUMNDEF identityColumn = {};
    identityColumn.cbStruct = sizeof(identityColumn);
    identityColumn.coltyp = JET_coltypLong;
    identityColumn.grbit = JET_bitColumnAutoincrement;
    JET_COLUMNID identityColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Identity",
                           &identityColumn, nullptr, 0,
                           &identityColumnId));
    JET_COLUMNDEF rankColumn = {};
    rankColumn.cbStruct = sizeof(rankColumn);
    rankColumn.coltyp = JET_coltypLong;
    rankColumn.grbit = JET_bitColumnNotNULL;
    JET_COLUMNID rankColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(), tableId, "Rank",
                           &rankColumn, nullptr, 0, &rankColumnId));
    //  Primary index on Identity so MoveFirst + Move(N) walks the
    //  insertion order deterministically; secondary "ByRank" so the
    //  SetCurrentIndex4 swap is observable (different ordering).
    CheckJet(JetCreateIndexA(session.Handle(), tableId, "PrimaryByIdentity",
                             JET_bitIndexPrimary | JET_bitIndexUnique,
                             "+Identity\0", 11, 80));
    CheckJet(JetCreateIndexA(session.Handle(), tableId, "ByRank",
                             JET_bitIndexUnique, "+Rank\0", 7, 80));

    //  Insert N rows with ranks N..1 so insertion order is the
    //  reverse of the ByRank index order.
    constexpr int32_t RowCount = 5;
    CheckJet(JetBeginTransaction(session.Handle()));
    for (int32_t i = 0; i < RowCount; ++i)
    {
        const int32_t rank = RowCount - i;
        CheckJet(JetPrepareUpdate(session.Handle(), tableId,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), tableId, rankColumnId,
                              &rank, sizeof(rank), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), tableId, nullptr, 0, nullptr));
    }
    CheckJet(JetCommitTransaction(session.Handle(), 0));

    //  Query JET_INDEXID for ByRank up front — JetGetTableIndexInfo
    //  doesn't require the cursor to be on the named index.
    JET_INDEXID byRankIndexId = {};
    CheckJet(JetGetTableIndexInfoA(session.Handle(), tableId,
                                   "ByRank",
                                   &byRankIndexId, sizeof(byRankIndexId),
                                   JET_IdxInfoIndexId));

    //  Position on the third row in the primary (Identity) index.
    CheckJet(JetMove(session.Handle(), tableId, JET_MoveFirst, 0));
    CheckJet(JetMove(session.Handle(), tableId, 2, 0));
    int32_t identityBefore = 0;
    uint32_t cb = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               identityColumnId,
                               &identityBefore, sizeof(identityBefore),
                               &cb, 0, nullptr));

    //  Use SetCurrentIndex4 with the cached JET_INDEXID + JET_bitNoMove
    //  + itagSequence=1 to swap indexes without losing the row.
    //  itagSequence=1 selects the primary entry on a possibly
    //  multi-valued index (we have only one value per row, so 1
    //  is the standard pick).
    CheckJet(JetSetCurrentIndex4A(session.Handle(), tableId,
                                  "ByRank", &byRankIndexId,
                                  JET_bitNoMove, /*itagSequence=*/1));

    int32_t identityAfter = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), tableId,
                               identityColumnId,
                               &identityAfter, sizeof(identityAfter),
                               &cb, 0, nullptr));
    Require(identityAfter == identityBefore);

    CheckJet(JetCloseTable(session.Handle(), tableId));
}

namespace
{

// Sparse-key setup used by the seek-comparison scenarios.  Keys are
// 10, 20, 30, 40, 50 so seeks targeting in-between values (15, 25,
// 35, ...) unambiguously resolve to one neighbor.  Returns the
// Identity column id.
JET_COLUMNID PopulateSparseKeyTable(EseTable& table,
                                    const std::vector<int32_t>& keys)
{
    const auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    auto sessionHandle = table.Database().Session().Handle();
    EseTransaction transaction(table.Database().Session());
    for (auto key : keys)
    {
        CheckJet(JetPrepareUpdate(sessionHandle, table.Id(), JET_prepInsert));
        CheckJet(JetSetColumn(sessionHandle, table.Id(),
                              identityColumnId,
                              &key, sizeof(key),
                              0, nullptr));
        CheckJet(JetUpdate(sessionHandle, table.Id(), nullptr, 0, nullptr));
    }
    transaction.Commit();
    return identityColumnId;
}

// Seek helper: build a single-int32 key, JetSeek with the supplied
// grbits, and return the engine's result code (so callers can assert
// JET_wrn* / JET_err* values directly).
JET_ERR SeekIntKey(EseTable& table, int32_t key, JET_GRBIT seekGrbit)
{
    auto sessionHandle = table.Database().Session().Handle();
    CheckJet(JetMakeKey(sessionHandle, table.Id(),
                        &key, sizeof(key), JET_bitNewKey));
    return JetSeek(sessionHandle, table.Id(), seekGrbit);
}

} // namespace

EseIntegrationScenario(Navigation, SeekLessThanLandsOnPredecessor)
{
    TemporaryDirectory directory("Navigation.SeekLessThanLandsOnPredecessor");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    auto seekResultIsHit = [](JET_ERR result)
    {
        return result == JET_errSuccess || result == JET_wrnSeekNotEqual;
    };

    // Seek with bitSeekLT for a key strictly between two existing
    // entries — the cursor lands on the largest entry that is strictly
    // less than the search key.  ESE has historically returned either
    // JET_errSuccess or JET_wrnSeekNotEqual here; we accept both and
    // pin the truth on the cursor's resulting position.
    Require(seekResultIsHit(SeekIntKey(table, 25, JET_bitSeekLT)));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 20);

    // Exact-match key on bitSeekLT still falls back to the strict
    // predecessor (LT excludes equal).
    Require(seekResultIsHit(SeekIntKey(table, 30, JET_bitSeekLT)));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 20);

    // No predecessor exists below the smallest key — the engine
    // returns JET_errRecordNotFound and leaves no current record.
    Require(SeekIntKey(table, 10, JET_bitSeekLT) == JET_errRecordNotFound);
    RequireJetError(JetRetrieveColumn(session.Handle(), table.Id(),
                                      identityColumnId,
                                      nullptr, 0, nullptr, 0, nullptr),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(Navigation, SeekLessThanOrEqualHitsExactOrPredecessor)
{
    TemporaryDirectory directory(
        "Navigation.SeekLessThanOrEqualHitsExactOrPredecessor");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    // Exact match: bitSeekLE returns JET_errSuccess and positions on
    // the exact key.
    Require(SeekIntKey(table, 30, JET_bitSeekLE) == JET_errSuccess);
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 30);

    // Between-keys lookup: bitSeekLE falls back to the largest entry
    // less than the search key, with the wrnSeekNotEqual warning.
    Require(SeekIntKey(table, 35, JET_bitSeekLE) == JET_wrnSeekNotEqual);
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 30);

    // No predecessor exists for keys below the table's minimum.
    Require(SeekIntKey(table, 5, JET_bitSeekLE) == JET_errRecordNotFound);
}

EseIntegrationScenario(Navigation, SeekGreaterThanLandsOnSuccessor)
{
    TemporaryDirectory directory(
        "Navigation.SeekGreaterThanLandsOnSuccessor");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    auto seekResultIsHit = [](JET_ERR result)
    {
        return result == JET_errSuccess || result == JET_wrnSeekNotEqual;
    };

    // Between-keys lookup: bitSeekGT lands on the smallest entry that
    // is strictly greater than the search key.  Like bitSeekLT, ESE
    // may signal the hit as either success or wrnSeekNotEqual.
    Require(seekResultIsHit(SeekIntKey(table, 25, JET_bitSeekGT)));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 30);

    // Exact key: bitSeekGT excludes equal — lands on the next entry.
    Require(seekResultIsHit(SeekIntKey(table, 30, JET_bitSeekGT)));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId)
            == 40);

    // Past the last key: no successor exists.
    Require(SeekIntKey(table, 50, JET_bitSeekGT) == JET_errRecordNotFound);
    Require(SeekIntKey(table, 75, JET_bitSeekGT) == JET_errRecordNotFound);
}

EseIntegrationScenario(Navigation, SeekEqualWithCheckUniquenessSignalsUniqueKey)
{
    TemporaryDirectory directory(
        "Navigation.SeekEqualWithCheckUniquenessSignalsUniqueKey");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "NonUnique");

    auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto categoryColumnId =
        table.AddColumn("Category", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // Secondary, non-unique index over Category.
    static constexpr std::string_view CategoryKey =
        std::string_view("+Category\0\0", 11);
    table.CreateIndex("ByCategory", CategoryKey, 0);

    // Categories: 100 has dupes (3 rows), 200 has dupes (2 rows),
    // 300 is unique (1 row).  bitCheckUniqueness on SeekEQ must
    // report JET_wrnUniqueKey for 300 and plain success for 100/200.
    const std::vector<int32_t> categories = { 100, 100, 100, 200, 200, 300 };
    {
        EseTransaction transaction(session);
        for (auto category : categories)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  categoryColumnId,
                                  &category, sizeof(category),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }
    (void)identityColumnId;

    // Switch to the secondary index so JetSeek operates on Category.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByCategory"));

    Require(SeekIntKey(table, 100,
                       JET_bitSeekEQ | JET_bitCheckUniqueness) == JET_errSuccess);
    Require(SeekIntKey(table, 200,
                       JET_bitSeekEQ | JET_bitCheckUniqueness) == JET_errSuccess);
    Require(SeekIntKey(table, 300,
                       JET_bitSeekEQ | JET_bitCheckUniqueness) == JET_wrnUniqueKey);

    // The unique-key seek must have landed on the actual unique
    // entry — read back to confirm.
    int32_t category = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                               categoryColumnId,
                               &category, sizeof(category),
                               &actualBytes, 0, nullptr));
    Require(category == 300);
}

EseIntegrationScenario(Navigation, SetIndexRangeExclusiveUpperBoundExcludesBoundary)
{
    TemporaryDirectory directory(
        "Navigation.SetIndexRangeExclusiveUpperBoundExcludesBoundary");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    // Position on the smallest key and establish an EXCLUSIVE upper
    // limit at 40 — the boundary value itself must be excluded.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t exclusiveUpperBound = 40;
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &exclusiveUpperBound, sizeof(exclusiveUpperBound),
                        JET_bitNewKey));
    CheckJet(JetSetIndexRange(session.Handle(), table.Id(),
                              JET_bitRangeUpperLimit));

    std::vector<int32_t> observed;
    while (true)
    {
        const auto value = RetrieveFixedColumnFromCurrentRecord<int32_t>(
            table, identityColumnId);
        observed.push_back(value);
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }

    // 40 must NOT appear in the walk — exclusive upper bound.
    const std::vector<int32_t> expected = { 10, 20, 30 };
    Require(observed.size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        Require(observed[index] == expected[index]);
    }
}

EseIntegrationScenario(Navigation, SetIndexRangeRemoveClearsActiveRange)
{
    TemporaryDirectory directory(
        "Navigation.SetIndexRangeRemoveClearsActiveRange");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    // Set an inclusive range bounded at 20 so the cursor's walk
    // observes only {10, 20}.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t upperLimit = 20;
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &upperLimit, sizeof(upperLimit), JET_bitNewKey));
    CheckJet(JetSetIndexRange(session.Handle(), table.Id(),
                              JET_bitRangeInclusive | JET_bitRangeUpperLimit));

    // Now clear the range with bitRangeRemove.  The Make/Seek key
    // payload is ignored on remove, but ESE still expects a valid
    // JetSetIndexRange call.
    CheckJet(JetSetIndexRange(session.Handle(), table.Id(),
                              JET_bitRangeRemove));

    // From the current position, the entire remainder of the index
    // must be visible again.
    std::vector<int32_t> observed;
    do
    {
        observed.push_back(
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId));
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);

    const std::vector<int32_t> expected = { 10, 20, 30, 40, 50 };
    Require(observed.size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        Require(observed[index] == expected[index]);
    }
}

EseIntegrationScenario(Navigation, SetIndexRangeInstantDurationDoesNotStick)
{
    TemporaryDirectory directory(
        "Navigation.SetIndexRangeInstantDurationDoesNotStick");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Sparse");
    auto identityColumnId =
        PopulateSparseKeyTable(table, { 10, 20, 30, 40, 50 });

    // Park the cursor on the smallest key, then validate with an
    // InstantDuration range whose upper bound is 20.  The current
    // key (10) falls inside, so JetSetIndexRange returns success.
    // The crucial property: the range does NOT remain active — a
    // subsequent MoveNext sweep must visit every row past 20, proving
    // the engine cleared the range as part of "instant duration".
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t probeUpperBound = 20;
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &probeUpperBound, sizeof(probeUpperBound),
                        JET_bitNewKey));
    CheckJet(JetSetIndexRange(session.Handle(), table.Id(),
                              JET_bitRangeInstantDuration
                              | JET_bitRangeInclusive
                              | JET_bitRangeUpperLimit));

    // If InstantDuration had stuck, the walk would terminate after 20.
    // It must not.
    std::vector<int32_t> observed;
    do
    {
        observed.push_back(
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table,
                                                          identityColumnId));
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);

    const std::vector<int32_t> expected = { 10, 20, 30, 40, 50 };
    Require(observed.size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        Require(observed[index] == expected[index]);
    }
}

EseIntegrationScenario(Navigation, MoveWithKeyNotEqualSkipsEqualKeyEntries)
{
    TemporaryDirectory directory(
        "Navigation.MoveWithKeyNotEqualSkipsEqualKeyEntries");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Navigation.mdb");
    EseTable table(database, "Categories");

    auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto categoryColumnId =
        table.AddColumn("Category", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // Non-unique secondary index over Category.
    static constexpr std::string_view CategoryKey =
        std::string_view("+Category\0\0", 11);
    table.CreateIndex("ByCategory", CategoryKey, 0);

    // Three categories, with multiple rows each: 100 × 3, 200 × 2,
    // 300 × 4.  Distinct keys on the secondary index: 100, 200, 300.
    const std::vector<int32_t> categories = {
        100, 100, 100, 200, 200, 300, 300, 300, 300
    };
    {
        EseTransaction transaction(session);
        for (auto category : categories)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(),
                                  categoryColumnId,
                                  &category, sizeof(category),
                                  0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }
    (void)identityColumnId;

    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(), "ByCategory"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // MoveKeyNE on MoveNext skips past every entry whose key equals
    // the current key — landing on the first row of the next distinct
    // key value.  Expected sequence: 100 -> 200 -> 300 -> end.
    std::vector<int32_t> distinctKeys;
    while (true)
    {
        const auto category = RetrieveFixedColumnFromCurrentRecord<int32_t>(
            table, categoryColumnId);
        distinctKeys.push_back(category);
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, JET_bitMoveKeyNE);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }

    const std::vector<int32_t> expected = { 100, 200, 300 };
    Require(distinctKeys.size() == expected.size());
    for (size_t index = 0; index < expected.size(); ++index)
    {
        Require(distinctKeys[index] == expected[index]);
    }
}


