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

EseIntegrationScenario(DataManipulation, InsertAndReadBackOneRow, Smoke)
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

EseIntegrationScenario(DataManipulation, InsertMultipleRowsRoundTrip, Smoke)
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

EseIntegrationScenario(DataManipulation, ReplaceUpdatesValueInPlace, Smoke)
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

EseIntegrationScenario(DataManipulation, DeleteRemovesCurrentRecord, Smoke)
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

EseIntegrationScenario(DataManipulation, CancelPreparedInsertLeavesNoRecord, Smoke)
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

EseIntegrationScenario(DataManipulation, SetColumnsBatchRoundTrip, Smoke)
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

EseIntegrationScenario(DataManipulation, RetrievingNullColumnReturnsColumnNullWarning, Smoke)
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

EseIntegrationScenario(DataManipulation, RollbackHidesUncommittedRow, Smoke)
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

EseIntegrationScenario(DataManipulation, EnumerateColumnsReturnsAllColumnValues, Smoke)
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

EseIntegrationScenario(DataManipulation, EnumerateColumnsCompressOutputReturnsSingleValueShape, Smoke)
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

EseIntegrationScenario(DataManipulation, EnumerateColumnsHonoursColumnIdSubset, Smoke)
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

EseIntegrationScenario(DataManipulation, RegisterCallbackFiresOnInsert, Smoke)
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

EseIntegrationScenario(DataManipulation, GetRecordSizeReportsNonZeroData, Smoke)
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
    //  Two non-tautological bounds: cbData must fit within a single
    //  page (the blob is sub-LV-threshold so the whole record lives
    //  inline), and cLongValues must be 0 because nothing was forced
    //  to the LV tree.
    Require(recsize.cbData < 32 * 1024);
    Require(recsize.cLongValues == 0);

    //  Round-trip the blob bytes — the engine reporting a non-zero
    //  cbData should correspond to actual stored payload, not just a
    //  populated size field.
    char retrievedBlob[256] = {};
    uint32_t retrievedBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), blobColumnId,
                               retrievedBlob, sizeof(retrievedBlob),
                               &retrievedBytes, 0, nullptr));
    Require(retrievedBytes == BlobPayload.size());
    Require(std::memcmp(retrievedBlob,
                        BlobPayload.data(),
                        BlobPayload.size()) == 0);
}

EseIntegrationScenario(DataManipulation, RetrieveTaggedColumnListReportsTaggedColumns, Smoke)
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

EseIntegrationScenario(DataManipulation, GetRecordSize2ReportsCompressedColumns, Smoke)
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

EseIntegrationScenario(DataManipulation, GetRecordSize3ReportsIntrinsicLongValueBytes, Smoke)
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

EseIntegrationScenario(DataManipulation, Update2WithBookmarkAndGrbitInserts, Smoke)
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

EseIntegrationScenario(DataManipulation, RetrieveColumnByReferenceReadsSeparatedLongValue, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveColumnByReferenceReadsSeparatedLongValue");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Refs");

    auto idColumn = table.AddColumn("Id", JET_coltypLong, JET_bitColumnNotNULL);
    auto blobColumn = table.AddColumn("Body", JET_coltypLongBinary);

    //  Pack a 16 KiB blob — comfortably above the ~8 KiB intrinsic-LV
    //  threshold so the engine spills it to the LV tree (separated).
    //  Separated LVs are what JET_bitRetrieveAsRefIfNotInRecord
    //  turns into a reference token.
    std::vector<uint8_t> blob(16 * 1024);
    for (size_t i = 0; i < blob.size(); ++i)
    {
        blob[i] = static_cast<uint8_t>((i * 31u + 17u) & 0xFFu);
    }
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
                              blobColumn,
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

    //  JetRetrieveColumn with JET_bitRetrieveAsRefIfNotInRecord on a
    //  separated LV returns JET_wrnColumnReference + writes the
    //  reference token (variable-length, opaque to the caller) into
    //  the caller's buffer.  The reference is what we hand to
    //  JetRetrieveColumnByReference to read the actual bytes.
    uint8_t reference[256] = {};
    uint32_t cbReference = 0;
    const auto refErr = JetRetrieveColumn(session.Handle(),
                                          table.Id(),
                                          blobColumn,
                                          reference,
                                          sizeof(reference),
                                          &cbReference,
                                          JET_bitRetrieveAsRefIfNotInRecord,
                                          nullptr);
    Require(refErr == JET_wrnColumnReference);
    Require(cbReference > 0);
    Require(cbReference <= sizeof(reference));

    //  Read the full blob through the reference — same bytes back.
    std::vector<uint8_t> readBack(blob.size());
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumnByReference(session.Handle(),
                                          table.Id(),
                                          reference,
                                          cbReference,
                                          /*ibData=*/0,
                                          readBack.data(),
                                          static_cast<uint32_t>(readBack.size()),
                                          &cbActual,
                                          0));
    Require(cbActual == blob.size());
    Require(std::memcmp(readBack.data(), blob.data(), blob.size()) == 0);

    //  Byte-range read: ibData=1024 skips the first 1 KiB.
    std::vector<uint8_t> middle(blob.size() - 1024);
    cbActual = 0;
    CheckJet(JetRetrieveColumnByReference(session.Handle(),
                                          table.Id(),
                                          reference,
                                          cbReference,
                                          /*ibData=*/1024,
                                          middle.data(),
                                          static_cast<uint32_t>(middle.size()),
                                          &cbActual,
                                          0));
    Require(cbActual == middle.size());
    Require(std::memcmp(middle.data(), blob.data() + 1024, middle.size()) == 0);

    //  JET_bitRetrievePhysicalSize via the by-reference path returns
    //  the on-disk byte count without copying any data.
    cbActual = 0;
    CheckJet(JetRetrieveColumnByReference(session.Handle(),
                                          table.Id(),
                                          reference,
                                          cbReference,
                                          /*ibData=*/0,
                                          /*pvData=*/nullptr,
                                          /*cbData=*/0,
                                          &cbActual,
                                          JET_bitRetrievePhysicalSize));
    Require(cbActual >= blob.size());
}

EseIntegrationScenario(DataManipulation, PrereadColumnsByReferenceReportsRequestedCount, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.PrereadColumnsByReferenceReportsRequestedCount");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Refs");

    auto idColumn = table.AddColumn("Id", JET_coltypLong, JET_bitColumnNotNULL);
    auto blobColumn = table.AddColumn("Body", JET_coltypLongBinary);

    constexpr int RowCount = 4;
    static constexpr uint32_t BlobSize = 16 * 1024;
    {
        EseTransaction transaction(session);
        std::vector<uint8_t> blob(BlobSize);
        for (int32_t r = 0; r < RowCount; ++r)
        {
            for (size_t i = 0; i < blob.size(); ++i)
            {
                blob[i] = static_cast<uint8_t>((r * 7u + i) & 0xFFu);
            }
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  idColumn,
                                  &r,
                                  sizeof(r),
                                  0,
                                  nullptr));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  blobColumn,
                                  blob.data(),
                                  BlobSize,
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

    //  Walk the rows, harvesting one reference token per row.
    std::vector<std::vector<uint8_t>> referenceStorage;
    std::vector<const void*> referencePointers;
    std::vector<uint32_t> referenceByteCounts;
    referenceStorage.reserve(RowCount);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    for (int row = 0; row < RowCount; ++row)
    {
        uint8_t reference[256] = {};
        uint32_t cbReference = 0;
        const auto refErr = JetRetrieveColumn(session.Handle(),
                                              table.Id(),
                                              blobColumn,
                                              reference,
                                              sizeof(reference),
                                              &cbReference,
                                              JET_bitRetrieveAsRefIfNotInRecord,
                                              nullptr);
        Require(refErr == JET_wrnColumnReference);
        Require(cbReference > 0);
        referenceStorage.emplace_back(reference, reference + cbReference);
        if (row + 1 < RowCount)
        {
            CheckJet(JetMove(session.Handle(),
                             table.Id(),
                             JET_MoveNext,
                             0));
        }
    }
    for (auto& ref : referenceStorage)
    {
        referencePointers.push_back(ref.data());
        referenceByteCounts.push_back(static_cast<uint32_t>(ref.size()));
    }

    //  Preread all four references into the cache.  Engine reports
    //  how many of them it actually initiated I/O for; on a freshly-
    //  written database the LV pages are still in cache so the
    //  count may be < RowCount.  Bound: the count must be reasonable
    //  (>= 0, <= RowCount).
    uint32_t referencesPreread = 0;
    CheckJet(JetPrereadColumnsByReference(session.Handle(),
                                          table.Id(),
                                          referencePointers.data(),
                                          referenceByteCounts.data(),
                                          static_cast<uint32_t>(
                                              referencePointers.size()),
                                          /*cPageCacheMin=*/1,
                                          /*cPageCacheMax=*/64,
                                          &referencesPreread,
                                          0));
    Require(referencesPreread <= static_cast<uint32_t>(referencePointers.size()));
}

EseIntegrationScenario(DataManipulation, StreamRecordsAndParseRoundTripsAllColumnsInIndexOrder, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.StreamRecordsAndParseRoundTripsAllColumnsInIndexOrder");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Stream");

    auto idColumn = table.AddColumn("Id",
                                    JET_coltypLong,
                                    JET_bitColumnNotNULL);
    auto valueColumn = table.AddColumn("Value",
                                       JET_coltypLong,
                                       JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKey =
        std::string_view("+Id\0\0", 5);
    table.CreateIndex("PrimaryById",
                      PrimaryKey,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    //  Populate four rows with paired (Id, Value) ints; Value = Id*10
    //  so the parsed stream is easy to verify.
    constexpr int RowCount = 4;
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < RowCount; ++i)
        {
            CheckJet(JetPrepareUpdate(session.Handle(),
                                      table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  idColumn,
                                  &i,
                                  sizeof(i),
                                  0,
                                  nullptr));
            const int32_t value = i * 10;
            CheckJet(JetSetColumn(session.Handle(),
                                  table.Id(),
                                  valueColumn,
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

    //  Position at the first row before streaming — the stream
    //  walks forward from the current cursor position.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    //  JetStreamRecords packs (record, column) tuples into a
    //  caller-allocated buffer with a private engine-defined layout
    //  (RECORD_BUFFER_HEADER_V1 + zero-or-more RECORD_BUFFER_COLUMN_VALUE
    //  entries).  4 records × 2 columns × ~32 bytes/tuple fits
    //  comfortably in 4 KiB.
    JET_COLUMNID columnsToFetch[2] = { idColumn, valueColumn };
    static constexpr uint32_t BufferBytes = 4096;
    std::vector<uint8_t> buffer(BufferBytes);
    uint32_t cbActual = 0;
    CheckJet(JetStreamRecords(session.Handle(),
                              table.Id(),
                              static_cast<uint32_t>(std::size(columnsToFetch)),
                              columnsToFetch,
                              buffer.data(),
                              BufferBytes,
                              &cbActual,
                              JET_bitStreamForward));
    Require(cbActual > 0);
    Require(cbActual <= BufferBytes);

    //  Truncate to the actual filled size — the parser uses cbData
    //  to decide where the stream ends.
    buffer.resize(cbActual);

    //  Walk the stream via JetRetrieveColumnFromRecordStream.
    //  Each call advances an internal cursor (held in the header at
    //  the start of the buffer) and reports the next column value's
    //  (iRecord, columnid, itagSequence, ibValue, cbValue).
    //  Returns JET_wrnNoMoreRecords when the buffer is fully parsed.
    int32_t idsObserved[RowCount] = {};
    int32_t valuesObserved[RowCount] = {};
    bool sawIdForRecord[RowCount] = {};
    bool sawValueForRecord[RowCount] = {};

    while (true)
    {
        uint32_t iRecord = 0;
        JET_COLUMNID columnid = 0;
        uint32_t itagSequence = 0;
        uint32_t ibValue = 0;
        uint32_t cbValue = 0;
        const auto err = JetRetrieveColumnFromRecordStream(buffer.data(),
                                                           static_cast<uint32_t>(buffer.size()),
                                                           &iRecord,
                                                           &columnid,
                                                           &itagSequence,
                                                           &ibValue,
                                                           &cbValue);
        if (err == JET_wrnNoMoreRecords)
        {
            break;
        }
        CheckJet(err);

        //  iRecord is 0-based: the engine initialises the header's
        //  iRecord to ulMax so the first new-record flip lands on 0.
        Require(iRecord < RowCount);
        Require(cbValue == sizeof(int32_t));
        Require(ibValue + cbValue <= buffer.size());

        int32_t value = 0;
        std::memcpy(&value, buffer.data() + ibValue, sizeof(value));

        const int recordIndex = static_cast<int>(iRecord);
        if (columnid == idColumn)
        {
            idsObserved[recordIndex] = value;
            sawIdForRecord[recordIndex] = true;
        }
        else if (columnid == valueColumn)
        {
            valuesObserved[recordIndex] = value;
            sawValueForRecord[recordIndex] = true;
        }
        else
        {
            Require(false);  //  unexpected column id in stream
        }
    }

    //  Every record's Id and Value should have appeared, exactly
    //  once each, and the pairing must match what we inserted.
    for (int i = 0; i < RowCount; ++i)
    {
        Require(sawIdForRecord[i]);
        Require(sawValueForRecord[i]);
        Require(idsObserved[i] == i);
        Require(valuesObserved[i] == i * 10);
    }
}

EseIntegrationScenario(DataManipulation, RetrieveCopyReadsStagedValueBeforeUpdate, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveCopyReadsStagedValueBeforeUpdate");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "CopyBuffer");

    auto columnId = table.AddColumn("Value", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr int32_t OriginalValue = 100;
    static constexpr int32_t StagedValue = 200;

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, columnId, OriginalValue);
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Begin a replace, stage the new value, but do NOT call JetUpdate
    // yet.  Two retrievals follow:
    //   * grbit=0 must read the committed (original) value from the
    //     record image,
    //   * JET_bitRetrieveCopy must read the staged copy buffer.
    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));
    CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                          &StagedValue, sizeof(StagedValue), 0, nullptr));

    int32_t committedRead = 0;
    uint32_t committedBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                               &committedRead, sizeof(committedRead),
                               &committedBytes, 0, nullptr));
    Require(committedBytes == sizeof(committedRead));
    Require(committedRead == OriginalValue);

    int32_t copyRead = 0;
    uint32_t copyBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), columnId,
                               &copyRead, sizeof(copyRead),
                               &copyBytes,
                               JET_bitRetrieveCopy, nullptr));
    Require(copyBytes == sizeof(copyRead));
    Require(copyRead == StagedValue);

    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    // After commit the record image is the staged value.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, columnId)
            == StagedValue);
}

EseIntegrationScenario(DataManipulation, RetrieveFromIndexReadsIndexEntryWithoutRecord, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveFromIndexReadsIndexEntryWithoutRecord");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "IndexedRead");

    auto keyColumnId = table.AddColumn("Key", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKeyDescriptor =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", PrimaryKeyDescriptor, JET_bitIndexPrimary);

    static constexpr int32_t SeededKey = 7;

    {
        EseTransaction transaction(session);
        InsertSingleFixedColumnRow<int32_t>(table, keyColumnId, SeededKey);
        transaction.Commit();
    }

    // Seek to the seeded row via the primary index.  JET_bitRetrieveFromIndex
    // tells the engine to return the column value from the index entry
    // itself rather than reading the record — this only works for
    // columns the current index covers, which is the case here.
    CheckJet(JetMakeKey(session.Handle(), table.Id(),
                        &SeededKey, sizeof(SeededKey), JET_bitNewKey));
    CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));

    int32_t fromIndex = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), keyColumnId,
                               &fromIndex, sizeof(fromIndex),
                               &actualBytes,
                               JET_bitRetrieveFromIndex, nullptr));
    Require(actualBytes == sizeof(fromIndex));
    Require(fromIndex == SeededKey);

    // Sanity: the unflagged retrieve agrees.
    Require(RetrieveFixedColumnFromCurrentRecord<int32_t>(table, keyColumnId)
            == SeededKey);
}

EseIntegrationScenario(DataManipulation, UpdateNoVersionAcceptedOnUncommittedTableRejectedAfterCommit, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.UpdateNoVersionAcceptedOnUncommittedTableRejectedAfterCommit");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");

    // JET_bitUpdateNoVersion skips the per-row undo image.  Because
    // there is no undo image, a rollback cannot remove these inserts —
    // the engine therefore restricts the flag to bulk loads into a
    // table that is still uncommitted (created in the current
    // transaction).  Past commit, the flag returns
    // JET_errUpdateMustVersion.

    static constexpr int32_t RowCount = 16;
    static constexpr std::string_view TableName = "BulkLoad";

    JET_COLUMNID columnId = JET_columnidNil;

    // 1) Inside an outer transaction: create the table and bulk-load
    //    every row with JET_bitUpdateNoVersion.  Commit; the data
    //    must persist.
    {
        EseTransaction transaction(session);
        EseTable bulkLoadTable(database, TableName);
        columnId = bulkLoadTable.AddColumn("Value", JET_coltypLong,
                                            JET_bitColumnNotNULL);
        for (int32_t rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), bulkLoadTable.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), bulkLoadTable.Id(),
                                  columnId,
                                  &rowIndex, sizeof(rowIndex),
                                  0, nullptr));
            CheckJet(JetUpdate2(session.Handle(), bulkLoadTable.Id(),
                                nullptr, 0, nullptr,
                                JET_bitUpdateNoVersion));
        }
        transaction.Commit();
    }

    // 2) Verify every bulk-loaded row is present and correct.
    {
        EseTable openedTable(database, TableName, EseTableMode::Open);
        int32_t observedCount = 0;
        CheckJet(JetMove(session.Handle(), openedTable.Id(),
                         JET_MoveFirst, 0));
        do
        {
            const auto value = RetrieveFixedColumnFromCurrentRecord<int32_t>(
                openedTable, columnId);
            Require(value == observedCount);
            ++observedCount;
        }
        while (JetMove(session.Handle(), openedTable.Id(), JET_MoveNext, 0)
               != JET_errNoCurrentRecord);
        Require(observedCount == RowCount);
    }

    // 3) Now that the table is committed, the same flag must be
    //    rejected with JET_errUpdateMustVersion.
    {
        EseTable openedTable(database, TableName, EseTableMode::Open);
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), openedTable.Id(),
                                  JET_prepInsert));
        const int32_t rejectedValue = 9999;
        CheckJet(JetSetColumn(session.Handle(), openedTable.Id(), columnId,
                              &rejectedValue, sizeof(rejectedValue),
                              0, nullptr));
        RequireJetError(JetUpdate2(session.Handle(), openedTable.Id(),
                                   nullptr, 0, nullptr,
                                   JET_bitUpdateNoVersion),
                        JET_errUpdateMustVersion);
        CheckJet(JetPrepareUpdate(session.Handle(), openedTable.Id(),
                                  JET_prepCancel));
        transaction.Commit();
    }
}

EseIntegrationScenario(DataManipulation, RetrievePageNumberReportsResidentPage, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrievePageNumberReportsResidentPage");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "PageHomes");

    auto valueColumnId = table.AddColumn("Value", JET_coltypLong,
                                          JET_bitColumnNotNULL);

    //  Insert enough rows to span multiple data pages so firstPage
    //  and lastPage end up on distinct pages — that's the load-bearing
    //  observable that proves JET_bitRetrievePageNumber tracks the
    //  cursor's actual position rather than returning a constant.
    //  4096 4-byte rows + per-record overhead are well above any
    //  supported page size (4–32 KiB).
    static constexpr int32_t RowCount = 4096;
    {
        EseTransaction transaction(session);
        for (int32_t rowIndex = 0; rowIndex < RowCount; ++rowIndex)
        {
            InsertSingleFixedColumnRow<int32_t>(table, valueColumnId, rowIndex);
        }
        transaction.Commit();
    }

    // JET_bitRetrievePageNumber returns the PGNO of the cursor's
    // current page as a 4-byte unsigned (fldext.cxx:36+).  The flag
    // must be set in isolation; columnid is ignored.  Validate that
    // (a) the call returns a non-zero PGNO for both first and last
    // rows, and (b) the two rows actually land on different pages.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    uint32_t firstPage = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumnId,
                               &firstPage, sizeof(firstPage),
                               &actualBytes,
                               JET_bitRetrievePageNumber, nullptr));
    Require(actualBytes == sizeof(firstPage));
    Require(firstPage > 0);

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveLast, 0));
    uint32_t lastPage = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), valueColumnId,
                               &lastPage, sizeof(lastPage),
                               &actualBytes,
                               JET_bitRetrievePageNumber, nullptr));
    Require(actualBytes == sizeof(lastPage));
    Require(lastPage > 0);
    //  With 4096 rows the first and last must be on different pages.
    //  This is the assertion that proves the engine actually consults
    //  cursor position to answer the query — not a "call returned
    //  something" check.
    Require(lastPage > firstPage);

    // Combining the flag with any other grbit is invalid — the engine
    // returns JET_errInvalidGrbit (fldext.cxx:41).
    uint32_t scratch = 0;
    const JET_ERR combinedResult =
        JetRetrieveColumn(session.Handle(), table.Id(), valueColumnId,
                          &scratch, sizeof(scratch),
                          &actualBytes,
                          JET_bitRetrievePageNumber | JET_bitRetrieveCopy,
                          nullptr);
    Require(combinedResult == JET_errInvalidGrbit);

    // Buffer too small returns JET_errBufferTooSmall.
    uint8_t shortBuffer[2] = {};
    const JET_ERR shortResult =
        JetRetrieveColumn(session.Handle(), table.Id(), valueColumnId,
                          shortBuffer, sizeof(shortBuffer),
                          &actualBytes,
                          JET_bitRetrievePageNumber, nullptr);
    Require(shortResult == JET_errBufferTooSmall);
}

EseIntegrationScenario(DataManipulation, RetrieveCopyIntrinsicReportsRemainingInlineCapacity, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveCopyIntrinsicReportsRemainingInlineCapacity");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Intrinsic");

    auto bodyColumnId = table.AddColumn("Body", JET_coltypLongBinary);

    // JET_bitRetrieveCopyIntrinsic only works inside an active
    // PrepareUpdate (fldext.cxx's ErrRECIGetIntrinsicAvail asserts
    // FFUCBUpdatePrepared).  Begin an insert, query before adding any
    // payload — the engine returns the inline-LV budget for this
    // column as a 4-byte ULONG.
    EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));

    uint32_t emptyRecordBudget = 0;
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), bodyColumnId,
                               &emptyRecordBudget, sizeof(emptyRecordBudget),
                               &actualBytes,
                               JET_bitRetrieveCopyIntrinsic, nullptr));
    Require(actualBytes == sizeof(emptyRecordBudget));
    Require(emptyRecordBudget > 0);

    // Fill some inline bytes; the remaining budget must shrink.
    static constexpr uint32_t IntrinsicFillBytes = 128;
    const std::vector<uint8_t> intrinsicFill(IntrinsicFillBytes, 0x55);
    CheckJet(JetSetColumn(session.Handle(), table.Id(), bodyColumnId,
                          intrinsicFill.data(),
                          static_cast<uint32_t>(intrinsicFill.size()),
                          JET_bitSetIntrinsicLV, nullptr));

    uint32_t budgetAfterFill = 0;
    CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), bodyColumnId,
                               &budgetAfterFill, sizeof(budgetAfterFill),
                               &actualBytes,
                               JET_bitRetrieveCopyIntrinsic, nullptr));
    Require(actualBytes == sizeof(budgetAfterFill));
    // The fill consumed inline space — the remaining budget is
    // smaller than the empty-record figure.
    Require(budgetAfterFill < emptyRecordBudget);

    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    // The persisted row round-trips the inline payload.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    const auto persistedPayload =
        RetrieveVariableColumnFromCurrentRecord(table, bodyColumnId,
                                                IntrinsicFillBytes);
    Require(persistedPayload.size() == IntrinsicFillBytes);
    Require(std::memcmp(persistedPayload.data(), intrinsicFill.data(),
                        IntrinsicFillBytes) == 0);
}

EseIntegrationScenario(DataManipulation, UpdateCheckESE97CompatibilityRejectsOversizeRecord, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.UpdateCheckESE97CompatibilityRejectsOversizeRecord");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Wide");

    // ESE97 vs modern record-size differ only in how multi-valued
    // tagged columns pack: ESE97 per-multivalue overhead is larger
    // (recupd.cxx around the cbESE98ColumnOverhead constants).  To
    // trigger JET_errRecordTooBigForBackwardCompatibility, we need a
    // record that fits modern (small per-MV overhead) but exceeds
    // ESE97's projected layout.  Many short multi-values does it.
    auto tagsColumnId = table.AddColumn(
        "Tags", JET_coltypBinary,
        JET_bitColumnTagged | JET_bitColumnMultiValued,
        /*maximumBytes*/ 4);

    // Probe the engine's record-size budget at runtime, then size the
    // multi-value count to fill modern record format almost to the
    // limit.  ESE97's larger per-MV overhead will push the projected
    // size over the page-record bound and trigger the
    // BackwardCompatibility error.
    JET_API_PTR recordSizeMost = 0;
    uint32_t paramBytes = sizeof(recordSizeMost);
    CheckJet(JetGetSystemParameterA(instance.Handle(), JET_sesidNil,
                                    JET_paramRecordSizeMost,
                                    &recordSizeMost, nullptr, paramBytes));
    // Per recupd.cxx, ESE98 overhead = 5-byte initial column +
    // 2-byte per multi-value; we use 4-byte payloads.  Pick a count
    // that fills ~80% of the modern budget so we comfortably commit
    // without exceeding it but ESE97 overhead will push past.
    static constexpr uint32_t PayloadBytes = 4;
    static constexpr uint32_t ModernOverheadPerMultiValue =
        PayloadBytes + 2;
    const uint32_t multiValueCount =
        static_cast<uint32_t>((recordSizeMost * 80 / 100)
                              / ModernOverheadPerMultiValue);
    const uint8_t payload[PayloadBytes] = { 0xAB, 0xCD, 0xEF, 0x01 };

    // Without the flag, JetUpdate accepts the long multi-value train —
    // modern record size handles it.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        for (uint32_t itag = 1; itag <= multiValueCount; ++itag)
        {
            JET_SETINFO setInformation = {};
            setInformation.cbStruct = sizeof(setInformation);
            setInformation.itagSequence = itag;
            CheckJet(JetSetColumn(session.Handle(), table.Id(), tagsColumnId,
                                  payload, sizeof(payload),
                                  0, &setInformation));
        }
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    // With JET_bitUpdateCheckESE97Compatibility, the engine projects
    // the record into ESE97's layout and refuses with
    // JET_errRecordTooBigForBackwardCompatibility because the ESE97
    // per-MV overhead pushes the projected size past the per-page
    // limit (REC::CbRecordMost with JET_cbKeyMost_OLD).
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        for (uint32_t itag = 1; itag <= multiValueCount; ++itag)
        {
            JET_SETINFO setInformation = {};
            setInformation.cbStruct = sizeof(setInformation);
            setInformation.itagSequence = itag;
            CheckJet(JetSetColumn(session.Handle(), table.Id(), tagsColumnId,
                                  payload, sizeof(payload),
                                  0, &setInformation));
        }
        RequireJetError(JetUpdate2(session.Handle(), table.Id(),
                                   nullptr, 0, nullptr,
                                   JET_bitUpdateCheckESE97Compatibility),
                        JET_errRecordTooBigForBackwardCompatibility);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
        transaction.Commit();
    }

    // The control row from the first commit is still present; no
    // extra row leaked from the rejected attempt.
    int32_t observedCount = 0;
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    do
    {
        ++observedCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedCount == 1);
}

EseIntegrationScenario(DataManipulation, RetrieveFromPrimaryBookmarkExtractsPrimaryKeyFromSecondaryEntry, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.RetrieveFromPrimaryBookmarkExtractsPrimaryKeyFromSecondaryEntry");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "PrimaryAndSecondary");

    auto identityColumnId =
        table.AddColumn("Identity", JET_coltypLong,
                        JET_bitColumnAutoincrement | JET_bitColumnNotNULL);
    auto categoryColumnId =
        table.AddColumn("Category", JET_coltypLong, JET_bitColumnNotNULL);

    static constexpr std::string_view PrimaryKeyDescriptor =
        std::string_view("+Identity\0\0", 11);
    table.CreateIndex("PrimaryByIdentity", PrimaryKeyDescriptor,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    // Secondary index over Category.  Its index entries internally
    // carry the primary key (Identity) as the bookmark.
    static constexpr std::string_view CategoryKeyDescriptor =
        std::string_view("+Category\0\0", 11);
    table.CreateIndex("ByCategory", CategoryKeyDescriptor, 0);

    // Seed rows with distinct Category values so each secondary entry
    // is unambiguous.
    const std::vector<int32_t> categories = { 100, 200, 300, 400, 500 };
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

    // JET_bitRetrieveFromPrimaryBookmark retrieves the primary-key
    // column from the bookmark bytes embedded in the secondary index
    // entry (fldext.cxx around line 1041+: pidb = pfcbTable->Pidb(),
    // the primary IDB).  The requested column must be IN the primary
    // index — Identity here.
    //
    // Walk on the secondary index, for each entry retrieve Identity
    // two ways: (a) JET_bitRetrieveFromPrimaryBookmark off the
    // secondary entry, (b) plain retrieve via record latch.  Both
    // must agree.
    CheckJet(JetSetCurrentIndexA(session.Handle(), table.Id(),
                                 "ByCategory"));
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t observedCount = 0;
    do
    {
        int32_t identityFromBookmark = 0;
        int32_t identityFromRecord = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   identityColumnId,
                                   &identityFromBookmark,
                                   sizeof(identityFromBookmark),
                                   &actualBytes,
                                   JET_bitRetrieveFromPrimaryBookmark,
                                   nullptr));
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(),
                                   identityColumnId,
                                   &identityFromRecord,
                                   sizeof(identityFromRecord),
                                   &actualBytes,
                                   0, nullptr));
        Require(identityFromBookmark == identityFromRecord);
        Require(identityFromBookmark > 0);
        ++observedCount;
    }
    while (JetMove(session.Handle(), table.Id(), JET_MoveNext, 0)
           != JET_errNoCurrentRecord);
    Require(observedCount == static_cast<int32_t>(categories.size()));

    // Negative path: a column that is NOT in the primary index
    // (Category itself is in the secondary, not primary) returns
    // JET_errColumnNotFound under FromPrimaryBookmark.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t scratch = 0;
    uint32_t scratchBytes = 0;
    const JET_ERR negativeResult =
        JetRetrieveColumn(session.Handle(), table.Id(),
                          categoryColumnId,
                          &scratch, sizeof(scratch),
                          &scratchBytes,
                          JET_bitRetrieveFromPrimaryBookmark, nullptr);
    Require(negativeResult == JET_errColumnNotFound);
}

EseIntegrationScenario(DataManipulation, EnumerateColumnsIgnoreDefaultMarksDefaultColumns, Smoke)
{
    TemporaryDirectory directory(
        "DataManipulation.EnumerateColumnsIgnoreDefaultMarksDefaultColumns");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Data.mdb");
    EseTable table(database, "Defaults");

    // Two TAGGED columns with explicit defaults; one column without.
    // JET_bitEnumerateIgnoreDefault affects how the engine merges the
    // default record image — only meaningful for tagged columns whose
    // default value sits in the default image rather than every
    // record's fixed-column slots.
    const int32_t firstDefaultValue = 11;
    const int32_t secondDefaultValue = 22;
    auto firstColumnId = table.AddColumnWithDefault(
        "First", JET_coltypLong,
        &firstDefaultValue, sizeof(firstDefaultValue),
        JET_bitColumnTagged);
    auto secondColumnId = table.AddColumnWithDefault(
        "Second", JET_coltypLong,
        &secondDefaultValue, sizeof(secondDefaultValue),
        JET_bitColumnTagged);
    auto plainColumnId = table.AddColumn("Plain", JET_coltypLong,
                                          JET_bitColumnTagged);

    // Insert one row that explicitly sets Second to a non-default
    // value and Plain to a real value; leave First at its default.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                  JET_prepInsert));
        const int32_t secondOverride = 99;
        const int32_t plainValue = 7;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), secondColumnId,
                              &secondOverride, sizeof(secondOverride),
                              0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), table.Id(), plainColumnId,
                              &plainValue, sizeof(plainValue),
                              0, nullptr));
        CheckJet(JetUpdate(session.Handle(), table.Id(),
                           nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Baseline: default-mode enumeration includes the column at its
    // default value (First).  fldenum.cxx:1981 sets fDefaultRecord =
    // !IgnoreDefault && tableHasDefault, so without the flag the
    // engine merges the default record image and First shows up with
    // the default value.
    {
        uint32_t cEnumColumn = 0;
        JET_ENUMCOLUMN* rgEnumColumn = nullptr;
        CheckJet(JetEnumerateColumns(session.Handle(), table.Id(),
                                     0, nullptr,
                                     &cEnumColumn, &rgEnumColumn,
                                     EnumerateColumnsRealloc, nullptr,
                                     0, 0));
        bool sawFirstAtDefault = false;
        for (uint32_t i = 0; i < cEnumColumn; ++i)
        {
            if (rgEnumColumn[i].columnid == firstColumnId
                && rgEnumColumn[i].err == JET_errSuccess
                && rgEnumColumn[i].cEnumColumnValue == 1)
            {
                int32_t firstValue = 0;
                std::memcpy(&firstValue,
                            rgEnumColumn[i].rgEnumColumnValue[0].pvData,
                            sizeof(firstValue));
                if (firstValue == firstDefaultValue)
                {
                    sawFirstAtDefault = true;
                }
            }
        }
        Require(sawFirstAtDefault);
        EnumerateColumnsRealloc(nullptr, rgEnumColumn, 0);
    }

    // With JET_bitEnumerateIgnoreDefault, the engine SKIPS the default
    // record image (fldenum.cxx:1981) — First was never explicitly set
    // so it is absent from the enumeration entirely.  Second (explicit
    // override) and Plain (explicit value) still appear with their
    // values.
    {
        uint32_t cEnumColumn = 0;
        JET_ENUMCOLUMN* rgEnumColumn = nullptr;
        CheckJet(JetEnumerateColumns(session.Handle(), table.Id(),
                                     0, nullptr,
                                     &cEnumColumn, &rgEnumColumn,
                                     EnumerateColumnsRealloc, nullptr,
                                     0, JET_bitEnumerateIgnoreDefault));
        bool sawFirst = false;
        bool sawSecondOverride = false;
        bool sawPlainValue = false;
        for (uint32_t i = 0; i < cEnumColumn; ++i)
        {
            if (rgEnumColumn[i].columnid == firstColumnId)
            {
                sawFirst = true;
            }
            else if (rgEnumColumn[i].columnid == secondColumnId)
            {
                Require(rgEnumColumn[i].err == JET_errSuccess);
                Require(rgEnumColumn[i].cEnumColumnValue == 1);
                int32_t storedSecond = 0;
                std::memcpy(&storedSecond,
                            rgEnumColumn[i].rgEnumColumnValue[0].pvData,
                            sizeof(storedSecond));
                Require(storedSecond == 99);
                sawSecondOverride = true;
            }
            else if (rgEnumColumn[i].columnid == plainColumnId)
            {
                Require(rgEnumColumn[i].err == JET_errSuccess);
                Require(rgEnumColumn[i].cEnumColumnValue == 1);
                int32_t storedPlain = 0;
                std::memcpy(&storedPlain,
                            rgEnumColumn[i].rgEnumColumnValue[0].pvData,
                            sizeof(storedPlain));
                Require(storedPlain == 7);
                sawPlainValue = true;
            }
        }
        Require(!sawFirst);
        Require(sawSecondOverride);
        Require(sawPlainValue);
        EnumerateColumnsRealloc(nullptr, rgEnumColumn, 0);
    }
}

//  ===================================================================
//  Tier::Regression — refactor-sensitivity scenarios
//
//  Targets B-tree behaviour that the Smoke tier doesn't exercise:
//  random-order insertion + delete-and-reinsert churn that drives
//  the freelist coalescing and split/merge code paths.
//  ===================================================================

namespace
{

//  Deterministic LCG so the regression scenarios are repeatable.
struct DeterministicShuffle
{
    uint32_t seed;
    uint32_t next()
    {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    }
};

}  // namespace

//  Insert 8192 rows in random key order, then walk the table in
//  ascending order and confirm every key is present exactly once.
//  Smoke tests use monotonic-ascending insertion; a refactor that
//  broke B-tree split selection on random-keys input would land
//  here.
EseIntegrationScenario(DataManipulation, RandomOrderInsertSurvivesAscendingWalk, Regression)
{
    TemporaryDirectory directory(
        "DataManipulation.RandomOrderInsertSurvivesAscendingWalk");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Random.mdb");
    EseTable table(database, "Rows");
    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    static constexpr std::string_view KeyIndex =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", KeyIndex,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr int32_t Count = 8192;
    std::vector<int32_t> shuffled(Count);
    for (int32_t i = 0; i < Count; ++i)
    {
        shuffled[static_cast<size_t>(i)] = i;
    }
    DeterministicShuffle rng{ 0xDEAD };
    for (int32_t i = Count - 1; i > 0; --i)
    {
        const int32_t j = static_cast<int32_t>(
            rng.next() % static_cast<uint32_t>(i + 1));
        std::swap(shuffled[static_cast<size_t>(i)],
                  shuffled[static_cast<size_t>(j)]);
    }

    {
        EseTransaction transaction(session);
        for (int32_t key : shuffled)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), keyColumn,
                                  &key, sizeof(key), 0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expected = 0;
    int32_t walkedRowCount = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), keyColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expected);
        ++expected;
        ++walkedRowCount;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(walkedRowCount == Count);
}

//  Reverse-order insert.  Smoke tests don't exercise right-to-left
//  growth at scale; a refactor that broke split direction or
//  rebalancing under descending input would land here.
EseIntegrationScenario(DataManipulation, ReverseOrderInsertProducesAscendingTable, Regression)
{
    TemporaryDirectory directory(
        "DataManipulation.ReverseOrderInsertProducesAscendingTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Reverse.mdb");
    EseTable table(database, "Rows");
    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    static constexpr std::string_view KeyIndex =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", KeyIndex,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr int32_t Count = 8192;
    {
        EseTransaction transaction(session);
        for (int32_t key = Count - 1; key >= 0; --key)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), keyColumn,
                                  &key, sizeof(key), 0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    int32_t expected = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), keyColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(observed == expected);
        ++expected;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(expected == Count);
}

//  Random-delete + reinsert churn — the freelist / page-reuse
//  exerciser.  4096 rows inserted, 50% randomly deleted, then a
//  fresh 2048 keys (offset to avoid collision) inserted, then
//  another 50% random delete from the surviving set.  Final walk
//  verifies every survivor is present in order with the right
//  value.  A refactor that broke freelist coalescing or
//  page-reuse heuristics would surface here as a corrupted walk.
EseIntegrationScenario(DataManipulation, ChurnFromDeleteReinsertPreservesAscendingOrder, Regression)
{
    TemporaryDirectory directory(
        "DataManipulation.ChurnFromDeleteReinsertPreservesAscendingOrder");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "Churn.mdb");
    EseTable table(database, "Rows");
    auto keyColumn = table.AddColumn("Key", JET_coltypLong,
                                     JET_bitColumnNotNULL);
    static constexpr std::string_view KeyIndex =
        std::string_view("+Key\0\0", 6);
    table.CreateIndex("PrimaryByKey", KeyIndex,
                      JET_bitIndexPrimary | JET_bitIndexUnique);

    static constexpr int32_t InitialCount = 4096;
    //  Insert 0..InitialCount-1 in random order.
    std::vector<int32_t> initial(InitialCount);
    for (int32_t i = 0; i < InitialCount; ++i)
    {
        initial[static_cast<size_t>(i)] = i;
    }
    DeterministicShuffle rng{ 0xBEEF };
    for (int32_t i = InitialCount - 1; i > 0; --i)
    {
        const int32_t j = static_cast<int32_t>(
            rng.next() % static_cast<uint32_t>(i + 1));
        std::swap(initial[static_cast<size_t>(i)],
                  initial[static_cast<size_t>(j)]);
    }
    {
        EseTransaction transaction(session);
        for (int32_t key : initial)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), keyColumn,
                                  &key, sizeof(key), 0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    //  Track which keys are alive.  Delete every other key (~50%)
    //  in random order to fragment the leaf pages.
    std::vector<bool> alive(InitialCount, true);
    {
        EseTransaction transaction(session);
        for (int32_t i = 0; i < InitialCount; i += 2)
        {
            const int32_t target = initial[static_cast<size_t>(i)];
            CheckJet(JetMakeKey(session.Handle(), table.Id(),
                                &target, sizeof(target), JET_bitNewKey));
            CheckJet(JetSeek(session.Handle(), table.Id(), JET_bitSeekEQ));
            CheckJet(JetDelete(session.Handle(), table.Id()));
            alive[static_cast<size_t>(target)] = false;
        }
        transaction.Commit();
    }

    //  Reinsert 2048 fresh keys (offset above the original range)
    //  in random order — exercises page reuse from the freelist.
    static constexpr int32_t RefillCount = 2048;
    static constexpr int32_t RefillOffset = 100000;
    std::vector<int32_t> refill(RefillCount);
    for (int32_t i = 0; i < RefillCount; ++i)
    {
        refill[static_cast<size_t>(i)] = RefillOffset + i;
    }
    for (int32_t i = RefillCount - 1; i > 0; --i)
    {
        const int32_t j = static_cast<int32_t>(
            rng.next() % static_cast<uint32_t>(i + 1));
        std::swap(refill[static_cast<size_t>(i)],
                  refill[static_cast<size_t>(j)]);
    }
    {
        EseTransaction transaction(session);
        for (int32_t key : refill)
        {
            CheckJet(JetPrepareUpdate(session.Handle(), table.Id(),
                                      JET_prepInsert));
            CheckJet(JetSetColumn(session.Handle(), table.Id(), keyColumn,
                                  &key, sizeof(key), 0, nullptr));
            CheckJet(JetUpdate(session.Handle(), table.Id(),
                               nullptr, 0, nullptr));
        }
        transaction.Commit();
    }

    //  Final walk: surviving original keys (the odd ones) appear in
    //  ascending order, followed by the 2048 refill keys in
    //  ascending order.  A refactor that corrupted a leaf page
    //  during reuse, double-counted, or dropped a survivor would
    //  surface here as a count mismatch or wrong-order value.
    std::vector<int32_t> expectedKeys;
    expectedKeys.reserve(InitialCount / 2 + RefillCount);
    for (int32_t k = 0; k < InitialCount; ++k)
    {
        if (alive[static_cast<size_t>(k)])
        {
            expectedKeys.push_back(k);
        }
    }
    for (int32_t i = 0; i < RefillCount; ++i)
    {
        expectedKeys.push_back(RefillOffset + i);
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    size_t cursor = 0;
    while (true)
    {
        int32_t observed = 0;
        uint32_t actualBytes = 0;
        CheckJet(JetRetrieveColumn(session.Handle(), table.Id(), keyColumn,
                                   &observed, sizeof(observed),
                                   &actualBytes, 0, nullptr));
        Require(cursor < expectedKeys.size());
        Require(observed == expectedKeys[cursor]);
        ++cursor;
        const auto moveResult = JetMove(session.Handle(), table.Id(),
                                        JET_MoveNext, 0);
        if (moveResult == JET_errNoCurrentRecord)
        {
            break;
        }
        CheckJet(moveResult);
    }
    Require(cursor == expectedKeys.size());
}

