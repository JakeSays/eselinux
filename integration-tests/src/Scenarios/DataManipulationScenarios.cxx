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
#include <string_view>
#include <vector>

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

EseIntegrationScenario(DataManipulation, GetRecordSizeReportsNonZeroData)
{
    TemporaryDirectory directory(
        "DataManipulation.GetRecordSizeReportsNonZeroData");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    auto idColumnId = table.AddColumn("Id", JET_coltypLong,
                                      JET_bitColumnNotNULL);
    auto blobColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static constexpr std::string_view BlobPayload =
        "the quick brown fox jumps over the lazy dog";
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t idValue = 1;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), idColumnId,
                              &idValue, sizeof(idValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), blobColumnId,
                              BlobPayload.data(),
                              static_cast<uint32_t>(BlobPayload.size()),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    JET_RECSIZE recsize = {};
    CheckJet(JetGetRecordSize(session.Handle(), table.Id(),
                              &recsize, 0));
    // The blob is too small to overflow into the long-value tree
    // (LV threshold is 8 KB by default).  cbData covers the inline
    // record bytes, including the Id column and the inline blob.
    Require(recsize.cbData >= BlobPayload.size());
    Require(recsize.cNonTaggedColumns + recsize.cTaggedColumns >= 2);
}

EseIntegrationScenario(DataManipulation, RetrieveTaggedColumnListReportsTaggedColumns)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveTaggedColumnListReportsTaggedColumns");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Tagged");

    //  JET_coltypLongBinary / LongText are tagged columns by default.
    //  Add a fixed Id (non-tagged) plus two tagged blobs so the
    //  enumeration has something to report.
    auto idColumn = table.AddColumn("Id", JET_coltypLong, JET_bitColumnNotNULL);
    auto blobA = table.AddColumn("BlobA", JET_coltypLongBinary);
    auto blobB = table.AddColumn("BlobB", JET_coltypLongBinary);

    static constexpr char BlobAPayload[] = "alpha";
    static constexpr char BlobBPayload[] = "bravo";

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        const int32_t id = 1;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              idColumn,
                              &id,
                              sizeof(id),
                              0,
                              nullptr));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              blobA,
                              BlobAPayload,
                              sizeof(BlobAPayload) - 1,
                              0,
                              nullptr));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              blobB,
                              BlobBPayload,
                              sizeof(BlobBPayload) - 1,
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    JET_RETRIEVEMULTIVALUECOUNT entries[16] = {};
    uint32_t cEntries = 0;
    CheckJet(JetRetrieveTaggedColumnList(session.Handle(),
                                         table.Id(),
                                         &cEntries,
                                         entries,
                                         sizeof(entries),
                                         /*columnidStart=*/0,
                                         0));
    //  Both blob columns must show up; the engine may also report the
    //  internal record-content columnid (0 or low value) — exact count
    //  isn't load-bearing, but we expect at least 2 tagged values.
    Require(cEntries >= 2);
    bool foundA = false;
    bool foundB = false;
    for (uint32_t i = 0; i < cEntries && i < std::size(entries); ++i)
    {
        if (entries[i].columnid == blobA)
        {
            foundA = true;
        }
        if (entries[i].columnid == blobB)
        {
            foundB = true;
        }
    }
    Require(foundA);
    Require(foundB);
}

EseIntegrationScenario(DataManipulation, GetRecordSize2ReportsCompressedColumns)
{
    TemporaryDirectory directory(
        "DataManipulation.GetRecordSize2ReportsCompressedColumns");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    auto idColumnId = table.AddColumn("Id",
                                      JET_coltypLong,
                                      JET_bitColumnNotNULL);
    auto blobColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    static constexpr std::string_view BlobPayload =
        "the quick brown fox jumps over the lazy dog";
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        const int32_t idValue = 1;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              idColumnId,
                              &idValue,
                              sizeof(idValue),
                              0,
                              nullptr));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              blobColumnId,
                              BlobPayload.data(),
                              static_cast<uint32_t>(BlobPayload.size()),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    //  JetGetRecordSize2 widens the v1 struct with cCompressedColumns
    //  + cbDataCompressed slots — useful when compression is enabled.
    //  Even without compression the v2 call must populate the v1
    //  fields correctly.
    JET_RECSIZE2 recsize = {};
    CheckJet(JetGetRecordSize2(session.Handle(),
                               table.Id(),
                               &recsize,
                               0));
    Require(recsize.cbData >= BlobPayload.size());
    Require(recsize.cNonTaggedColumns + recsize.cTaggedColumns >= 2);
    //  cbDataCompressed equals cbData when no LV is compressed —
    //  the v2-specific field must be populated, not left as zero.
    Require(recsize.cbDataCompressed > 0);
}

EseIntegrationScenario(DataManipulation, GetRecordSize3ReportsIntrinsicLongValueBytes)
{
    TemporaryDirectory directory(
        "DataManipulation.GetRecordSize3ReportsIntrinsicLongValueBytes");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    auto idColumnId = table.AddColumn("Id",
                                      JET_coltypLong,
                                      JET_bitColumnNotNULL);
    auto blobColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    //  Small blob — well under the separation threshold — so the
    //  engine keeps the bytes inline as an intrinsic LV instead of
    //  spilling to the long-value B-tree.  That's the storage path
    //  cIntrinsicLongValues / cbIntrinsicLongValueData track.
    std::vector<uint8_t> blob(256, 0xAB);
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        const int32_t idValue = 1;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              idColumnId,
                              &idValue,
                              sizeof(idValue),
                              0,
                              nullptr));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              blobColumnId,
                              blob.data(),
                              static_cast<uint32_t>(blob.size()),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    //  JetGetRecordSize3 extends v2 with intrinsic-LV accounting:
    //  cIntrinsicLongValues counts intrinsic blobs in the record,
    //  cbIntrinsicLongValueData reports their bytes.  Our 2 KiB blob
    //  fits inline (LV threshold is 8 KiB) so it shows up in those
    //  intrinsic-LV fields — cbData itself tracks only fixed/
    //  variable column data, not the intrinsic LV bytes.
    JET_RECSIZE3 recsize = {};
    CheckJet(JetGetRecordSize3(session.Handle(),
                               table.Id(),
                               &recsize,
                               0));
    Require(recsize.cIntrinsicLongValues >= 1);
    Require(recsize.cbIntrinsicLongValueData >= blob.size());
    //  The blob is stored intrinsically, so cbLongValueData (LV-tree
    //  storage) must NOT count it.
    Require(recsize.cbLongValueData == 0);
}

EseIntegrationScenario(DataManipulation, Update2WithBookmarkAndGrbitInserts)
{
    TemporaryDirectory directory(
        "DataManipulation.Update2WithBookmarkAndGrbitInserts");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Rows");

    auto columnId = table.AddColumn("Value",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Value\0\0", 8);
    table.CreateIndex("PrimaryByValue", PrimaryKey, JET_bitIndexPrimary);

    //  JetUpdate2 extends JetUpdate with a grbit parameter.  Calling
    //  it with grbit=0 must behave identically to JetUpdate — emit
    //  the inserted record to disk AND populate the bookmark
    //  out-param when one is requested.
    //
    //  (The documented JET_bitUpdateNoVersion is only valid on
    //  uncommitted tables — applying it to a normal post-create row
    //  surfaces JET_errUpdateMustVersion.  That contract is
    //  separately verifiable but distracting from this scenario's
    //  goal.)
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        const int32_t value = 7777;
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              columnId,
                              &value,
                              sizeof(value),
                              0,
                              nullptr));

        uint8_t bookmark[JET_cbBookmarkMost] = {};
        uint32_t cbBookmark = 0;
        CheckJet(JetUpdate2(session.Handle(),
                            table.Id(),
                            bookmark,
                            sizeof(bookmark),
                            &cbBookmark,
                            0));
        Require(cbBookmark > 0);
        Require(cbBookmark <= sizeof(bookmark));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId)
            == 7777);
}
