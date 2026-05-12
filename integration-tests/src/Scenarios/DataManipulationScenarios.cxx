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

#include <cstdlib>
#include <cstring>

using namespace ese::tests;

EseIntegrationScenario(DataManipulation, InsertAndReadBackOneRow)
{
    TemporaryDirectory directory("DataManipulation.InsertAndReadBackOneRow");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "OneRow");

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
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Sequence");

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
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Mutable");

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
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Deletable");

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

    // Only one record should remain.
    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveNext, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(DataManipulation, CancelPreparedInsertLeavesNoRecord)
{
    TemporaryDirectory directory(
        "DataManipulation.CancelPreparedInsertLeavesNoRecord");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Empty");

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
        // Cancel — no JetUpdate call.
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
        transaction.Commit();
    }

    // Table must be empty.
    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

EseIntegrationScenario(DataManipulation, SetColumnsBatchRoundTrip)
{
    TemporaryDirectory directory(
        "DataManipulation.SetColumnsBatchRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "WideRow");

    const auto firstId = table.AddColumn("First", JET_coltypLong);
    const auto secondId = table.AddColumn("Second", JET_coltypLong);
    const auto thirdId = table.AddColumn("Third", JET_coltypLong);

    const int32_t firstValue = 100;
    const int32_t secondValue = 200;
    const int32_t thirdValue = 300;

    JET_SETCOLUMN setColumns[3] = { {}, {}, {} };
    setColumns[0].columnid = firstId;
    setColumns[0].pvData = &firstValue;
    setColumns[0].cbData = sizeof(firstValue);
    setColumns[1].columnid = secondId;
    setColumns[1].pvData = &secondValue;
    setColumns[1].cbData = sizeof(secondValue);
    setColumns[2].columnid = thirdId;
    setColumns[2].pvData = &thirdValue;
    setColumns[2].cbData = sizeof(thirdValue);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetSetColumns(session.Handle(), table.Id(), setColumns, 3));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t readFirst = 0;
    int32_t readSecond = 0;
    int32_t readThird = 0;
    JET_RETRIEVECOLUMN retrieveColumns[3] = { {}, {}, {} };
    retrieveColumns[0].columnid = firstId;
    retrieveColumns[0].pvData = &readFirst;
    retrieveColumns[0].cbData = sizeof(readFirst);
    retrieveColumns[0].itagSequence = 1;
    retrieveColumns[1].columnid = secondId;
    retrieveColumns[1].pvData = &readSecond;
    retrieveColumns[1].cbData = sizeof(readSecond);
    retrieveColumns[1].itagSequence = 1;
    retrieveColumns[2].columnid = thirdId;
    retrieveColumns[2].pvData = &readThird;
    retrieveColumns[2].cbData = sizeof(readThird);
    retrieveColumns[2].itagSequence = 1;

    CheckJet(JetRetrieveColumns(session.Handle(), table.Id(), retrieveColumns, 3));

    Require(readFirst == firstValue);
    Require(readSecond == secondValue);
    Require(readThird == thirdValue);
}

EseIntegrationScenario(DataManipulation, RetrievingNullColumnReturnsColumnNullWarning)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrievingNullColumnReturnsColumnNullWarning");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "WithNull");

    auto identityColumnId = table.AddColumn("Identity",
                                            JET_coltypLong,
                                            JET_bitColumnAutoincrement);
    auto nullableColumnId = table.AddColumn("Nullable", JET_coltypLong);

    // Insert a row that doesn't touch the nullable column.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t scratch = 0;
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
    Require(actualSize == 0);

    // The identity column is still readable on the same record.
    auto identity =
        RetrieveFixedColumnFromCurrentRecord<int32_t>(table, identityColumnId);
    Require(identity > 0);
}

EseIntegrationScenario(DataManipulation, RollbackHidesUncommittedRow)
{
    TemporaryDirectory directory(
        "DataManipulation.RollbackHidesUncommittedRow");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rollback");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 11);
        transaction.Rollback();
    }

    RequireJetError(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0),
                    JET_errNoCurrentRecord);
}

namespace
{

// JET_PFNREALLOC callback for JetEnumerateColumns.  The engine asks
// the caller to grow the output buffer; standard realloc is sufficient
// since the buffer it manages is heap-allocated and the engine frees
// it on cleanup via the same callback (passing cb=0 == free).
void* JET_API EnumerateColumnsRealloc(void*    /*pvContext*/,
                                      void*    pv,
                                      uint32_t cb)
{
    if (cb == 0)
    {
        free(pv);
        return nullptr;
    }
    return realloc(pv, cb);
}

// Counter incremented by the callback below; per-scenario state lives
// in the pvContext pointer the engine round-trips for us.
struct CallbackCounters
{
    int beforeInsert = 0;
    int afterInsert  = 0;
};

JET_ERR JET_API RecordCallback(JET_SESID      /*sesid*/,
                               JET_DBID       /*dbid*/,
                               JET_TABLEID    /*tableid*/,
                               JET_CBTYP      cbtyp,
                               void*          /*pvArg1*/,
                               void*          /*pvArg2*/,
                               void*          pvContext,
                               JET_API_PTR    /*ulUnused*/)
{
    auto* const counters = static_cast<CallbackCounters*>(pvContext);
    if (cbtyp & JET_cbtypBeforeInsert)
    {
        ++counters->beforeInsert;
    }
    if (cbtyp & JET_cbtypAfterInsert)
    {
        ++counters->afterInsert;
    }
    return JET_errSuccess;
}

} // namespace

EseIntegrationScenario(DataManipulation, EnumerateColumnsReturnsAllColumnValues)
{
    TemporaryDirectory directory(
        "DataManipulation.EnumerateColumnsReturnsAllColumnValues");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    const auto idColumnId    = table.AddColumn("Id",    JET_coltypLong);
    const auto valueColumnId = table.AddColumn("Value", JET_coltypLong);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t idValue    = 7;
        const int32_t valueValue = 91;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), idColumnId,
                              &idValue, sizeof(idValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), valueColumnId,
                              &valueValue, sizeof(valueValue), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // cEnumColumnId == 0 + rgEnumColumnId == nullptr asks the engine to
    // enumerate every set column of the current record.
    uint32_t cEnumColumn = 0;
    JET_ENUMCOLUMN* rgEnumColumn = nullptr;
    CheckJet(JetEnumerateColumns(session.Handle(), table.Id(),
                                 0, nullptr,
                                 &cEnumColumn, &rgEnumColumn,
                                 EnumerateColumnsRealloc, nullptr,
                                 0, 0));
    Require(cEnumColumn == 2);

    // Without JET_bitEnumerateCompressOutput the engine reports each
    // column via the multi-value branch (cEnumColumnValue / rgEnumColumnValue),
    // even when the column has only one value.
    int32_t idObserved    = 0;
    int32_t valueObserved = 0;
    bool sawId    = false;
    bool sawValue = false;
    for (uint32_t i = 0; i < cEnumColumn; ++i)
    {
        Require(rgEnumColumn[i].err == JET_errSuccess);
        Require(rgEnumColumn[i].cEnumColumnValue == 1);
        Require(rgEnumColumn[i].rgEnumColumnValue != nullptr);

        const auto& enumValue = rgEnumColumn[i].rgEnumColumnValue[0];
        Require(enumValue.err == JET_errSuccess);
        Require(enumValue.cbData == sizeof(int32_t));
        Require(enumValue.pvData != nullptr);

        int32_t bits = 0;
        std::memcpy(&bits, enumValue.pvData, sizeof(bits));
        if (rgEnumColumn[i].columnid == idColumnId)
        {
            idObserved = bits;
            sawId = true;
        }
        else if (rgEnumColumn[i].columnid == valueColumnId)
        {
            valueObserved = bits;
            sawValue = true;
        }
    }
    Require(sawId);
    Require(sawValue);
    Require(idObserved == 7);
    Require(valueObserved == 91);

    EnumerateColumnsRealloc(nullptr, rgEnumColumn, 0);
}

EseIntegrationScenario(DataManipulation, EnumerateColumnsCompressOutputReturnsSingleValueShape)
{
    TemporaryDirectory directory(
        "DataManipulation.EnumerateColumnsCompressOutputReturnsSingleValueShape");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    const auto idColumnId    = table.AddColumn("Id",    JET_coltypLong);
    const auto valueColumnId = table.AddColumn("Value", JET_coltypLong);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t idValue    = 11;
        const int32_t valueValue = 99;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), idColumnId,
                              &idValue, sizeof(idValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), valueColumnId,
                              &valueValue, sizeof(valueValue), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    uint32_t cEnumColumn = 0;
    JET_ENUMCOLUMN* rgEnumColumn = nullptr;
    CheckJet(JetEnumerateColumns(session.Handle(), table.Id(),
                                 0, nullptr,
                                 &cEnumColumn, &rgEnumColumn,
                                 EnumerateColumnsRealloc, nullptr,
                                 0, JET_bitEnumerateCompressOutput));
    Require(cEnumColumn == 2);
    for (uint32_t i = 0; i < cEnumColumn; ++i)
    {
        // CompressOutput flips single-valued columns into the inline shape.
        Require(rgEnumColumn[i].err == JET_wrnColumnSingleValue);
        Require(rgEnumColumn[i].cbData == sizeof(int32_t));
        Require(rgEnumColumn[i].pvData != nullptr);
    }
    EnumerateColumnsRealloc(nullptr, rgEnumColumn, 0);
}

EseIntegrationScenario(DataManipulation, EnumerateColumnsHonoursColumnIdSubset)
{
    TemporaryDirectory directory(
        "DataManipulation.EnumerateColumnsHonoursColumnIdSubset");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    const auto first  = table.AddColumn("First",  JET_coltypLong);
    const auto second = table.AddColumn("Second", JET_coltypLong);
    const auto third  = table.AddColumn("Third",  JET_coltypLong);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        const int32_t firstValue  = 1;
        const int32_t secondValue = 2;
        const int32_t thirdValue  = 3;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), first,
                              &firstValue, sizeof(firstValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), second,
                              &secondValue, sizeof(secondValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), third,
                              &thirdValue, sizeof(thirdValue), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Request just first + third — second is intentionally omitted.
    JET_ENUMCOLUMNID enumIds[2] = {};
    enumIds[0].columnid     = first;
    enumIds[0].ctagSequence = 0;
    enumIds[0].rgtagSequence = nullptr;
    enumIds[1].columnid     = third;
    enumIds[1].ctagSequence = 0;
    enumIds[1].rgtagSequence = nullptr;

    uint32_t cEnumColumn = 0;
    JET_ENUMCOLUMN* rgEnumColumn = nullptr;
    CheckJet(JetEnumerateColumns(session.Handle(), table.Id(),
                                 2, enumIds,
                                 &cEnumColumn, &rgEnumColumn,
                                 EnumerateColumnsRealloc, nullptr,
                                 0, 0));
    Require(cEnumColumn == 2);
    // Order matches the request; engine doesn't reorder.
    Require(rgEnumColumn[0].columnid == first);
    Require(rgEnumColumn[1].columnid == third);

    EnumerateColumnsRealloc(nullptr, rgEnumColumn, 0);
}

EseIntegrationScenario(DataManipulation, RegisterCallbackFiresOnInsert)
{
    TemporaryDirectory directory(
        "DataManipulation.RegisterCallbackFiresOnInsert");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");
    auto columnId = table.AddColumn("Value", JET_coltypLong,
                                    JET_bitColumnNotNULL);

    CallbackCounters counters;
    JET_HANDLE callbackId = 0;
    CheckJet(JetRegisterCallback(session.Handle(), table.Id(),
                                 JET_cbtypBeforeInsert | JET_cbtypAfterInsert,
                                 RecordCallback, &counters, &callbackId));

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 1);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 2);
        transaction.Commit();
    }
    Require(counters.beforeInsert == 2);
    Require(counters.afterInsert == 2);

    // After unregister, further inserts must not call the callback.
    CheckJet(JetUnregisterCallback(session.Handle(), table.Id(),
                                   JET_cbtypBeforeInsert | JET_cbtypAfterInsert,
                                   callbackId));

    const auto baseline = counters;
    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, 3);
        transaction.Commit();
    }
    Require(counters.beforeInsert == baseline.beforeInsert);
    Require(counters.afterInsert == baseline.afterInsert);
}
