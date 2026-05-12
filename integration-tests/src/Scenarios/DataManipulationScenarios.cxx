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

using namespace ese::tests;

EseIntegrationScenario(DataManipulation, InsertAndReadBackOneRow)
{
    TemporaryDirectory directory("DataManipulation.InsertAndReadBackOneRow");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Data.mdb");
    EseTable           table(database, "OneRow");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 0x12345678);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack = RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(readBack == 0x12345678);
}

EseIntegrationScenario(DataManipulation, InsertMultipleRowsRoundTrip)
{
    TemporaryDirectory directory("DataManipulation.InsertMultipleRowsRoundTrip");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Data.mdb");
    EseTable           table(database, "Sequence");

    auto columnId = table.AddColumn("Sequence", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr int32_t RowCount = 25;
    {
        EseTransaction transaction(session);
        for (int32_t valueToInsert = 0; valueToInsert < RowCount; ++valueToInsert)
        {
            InsertSingleFixedColumnRow<int32_t>(table, columnId, valueToInsert);
        }
        transaction.Commit();
    }

    int32_t observed = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        const auto value =
            RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
        Require(value == observed);
        ++observed;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0) != JET_errNoCurrentRecord);

    Require(observed == RowCount);
}

EseIntegrationScenario(DataManipulation, ReplaceUpdatesValueInPlace)
{
    TemporaryDirectory directory("DataManipulation.ReplaceUpdatesValueInPlace");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Data.mdb");
    EseTable           table(database, "Mutable");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));
        const int32_t replacedValue = 999;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              columnId,
                              &replacedValue,
                              sizeof(replacedValue),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto readBack = RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(readBack == 999);
}

EseIntegrationScenario(DataManipulation, DeleteRemovesCurrentRecord)
{
    TemporaryDirectory directory("DataManipulation.DeleteRemovesCurrentRecord");
    EseInstance        instance(directory);
    EseSession         session(instance);
    EseDatabase        database(session, "Data.mdb");
    EseTable           table(database, "Deletable");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 7);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 11);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    {
        EseTransaction transaction(session);
        CheckJet(JetDelete(session.Handle(), table.Id()));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    auto survivor =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId);
    Require(survivor == 11);

    //  Only one record should remain.
    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(DataManipulation, CancelPreparedInsertLeavesNoRecord)
{
    TemporaryDirectory directory(
        "DataManipulation.CancelPreparedInsertLeavesNoRecord");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable    table(database, "Empty");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t unwantedValue = 42;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              columnId,
                              &unwantedValue,
                              sizeof(unwantedValue),
                              0,
                              nullptr));
        //  Cancel — no JetUpdate call.
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
        transaction.Commit();
    }

    //  Table must be empty.
    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(DataManipulation, SetColumnsBatchRoundTrip)
{
    TemporaryDirectory directory(
        "DataManipulation.SetColumnsBatchRoundTrip");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable    table(database, "WideRow");

    const auto firstId   = table.AddColumn("First",   JET_coltypLong);
    const auto secondId  = table.AddColumn("Second",  JET_coltypLong);
    const auto thirdId   = table.AddColumn("Third",   JET_coltypLong);

    const int32_t firstValue  = 100;
    const int32_t secondValue = 200;
    const int32_t thirdValue  = 300;

    JET_SETCOLUMN setColumns[3] = { {}, {}, {} };
    setColumns[0].columnid = firstId;
    setColumns[0].pvData   = &firstValue;
    setColumns[0].cbData   = sizeof(firstValue);
    setColumns[1].columnid = secondId;
    setColumns[1].pvData   = &secondValue;
    setColumns[1].cbData   = sizeof(secondValue);
    setColumns[2].columnid = thirdId;
    setColumns[2].pvData   = &thirdValue;
    setColumns[2].cbData   = sizeof(thirdValue);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumns(session.Handle(), table.Id(), setColumns, 3));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t readFirst  = 0;
    int32_t readSecond = 0;
    int32_t readThird  = 0;
    JET_RETRIEVECOLUMN retrieveColumns[3] = { {}, {}, {} };
    retrieveColumns[0].columnid     = firstId;
    retrieveColumns[0].pvData       = &readFirst;
    retrieveColumns[0].cbData       = sizeof(readFirst);
    retrieveColumns[0].itagSequence = 1;
    retrieveColumns[1].columnid     = secondId;
    retrieveColumns[1].pvData       = &readSecond;
    retrieveColumns[1].cbData       = sizeof(readSecond);
    retrieveColumns[1].itagSequence = 1;
    retrieveColumns[2].columnid     = thirdId;
    retrieveColumns[2].pvData       = &readThird;
    retrieveColumns[2].cbData       = sizeof(readThird);
    retrieveColumns[2].itagSequence = 1;

    CheckJet(JetRetrieveColumns(session.Handle(), table.Id(), retrieveColumns, 3));

    Require(readFirst  == firstValue);
    Require(readSecond == secondValue);
    Require(readThird  == thirdValue);
}

EseIntegrationScenario(DataManipulation, RetrievingNullColumnReturnsColumnNullWarning)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrievingNullColumnReturnsColumnNullWarning");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable    table(database, "WithNull");

    auto identityColumnId = table.AddColumn("Identity",
                                            JET_coltypLong,
                                            JET_bitColumnAutoincrement);
    auto nullableColumnId = table.AddColumn("Nullable", JET_coltypLong);

    //  Insert a row that doesn't touch the nullable column.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t  scratch  = 0;
    uint32_t actualSize = 0;
    auto warningOrError = JetRetrieveColumn(session.Handle(),
                                            table.Id(),
                                            nullableColumnId,
                                            &scratch,
                                            sizeof(scratch),
                                            &actualSize,
                                            0,
                                            nullptr);
    Require(warningOrError == JET_wrnColumnNull);
    Require(actualSize     == 0);

    //  The identity column is still readable on the same record.
    auto identity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    Require(identity > 0);
}

EseIntegrationScenario(DataManipulation, RollbackHidesUncommittedRow)
{
    TemporaryDirectory directory(
        "DataManipulation.RollbackHidesUncommittedRow");
    EseInstance instance(directory);
    EseSession  session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable    table(database, "Rollback");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 11);
        transaction.Rollback();
    }

    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}
