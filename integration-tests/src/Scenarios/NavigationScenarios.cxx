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
