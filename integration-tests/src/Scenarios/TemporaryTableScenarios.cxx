// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

namespace
{

// Open a temp table with a single Long key column.  Returns the
// JET_TABLEID and writes the column id to *columnId.  Caller closes.
JET_TABLEID OpenLongKeyTempTable(EseSession& session,
                                 JET_GRBIT flags,
                                 JET_COLUMNID* columnId,
                                 JET_GRBIT keyColumnFlags = JET_bitColumnTTKey)
{
    JET_COLUMNDEF columnDefinitions[1] = { {} };
    columnDefinitions[0].cbStruct = sizeof(columnDefinitions[0]);
    columnDefinitions[0].coltyp = JET_coltypLong;
    columnDefinitions[0].grbit = keyColumnFlags;

    JET_COLUMNID columnIdentifiers[1] = { 0 };
    JET_TABLEID temporaryTableId = JET_tableidNil;
    CheckJet(JetOpenTempTable(session.Handle(),
                              columnDefinitions,
                              1,
                              flags,
                              &temporaryTableId,
                              columnIdentifiers));
    *columnId = columnIdentifiers[0];
    return temporaryTableId;
}

void InsertLong(JET_SESID session, JET_TABLEID table, JET_COLUMNID column, int32_t value)
{
    CheckJet(JetPrepareUpdate(session, table, JET_prepInsert));
    CheckJet(JetSetColumn(session, table, column, &value, sizeof(value), 0, nullptr));
    CheckJet(JetUpdate(session, table, nullptr, 0, nullptr));
}

int32_t RetrieveCurrentLong(JET_SESID session, JET_TABLEID table, JET_COLUMNID column)
{
    int32_t value = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(session, table, column,
                               &value, sizeof(value), &actualSize, 0, nullptr));
    return value;
}

}  // namespace

EseIntegrationScenario(TemporaryTable, OpenSortedTempTable)
{
    TemporaryDirectory directory("TemporaryTable.OpenSortedTempTable");
    EseInstance instance(directory);
    EseSession session(instance);

    JET_COLUMNID columnId = 0;
    auto temporaryTableId = OpenLongKeyTempTable(session,
                                                 JET_bitTTUpdatable,
                                                 &columnId);
    Require(temporaryTableId != JET_tableidNil);
    Require(columnId != 0);
    CheckJet(JetCloseTable(session.Handle(), temporaryTableId));
}

EseIntegrationScenario(TemporaryTable, AscendingKeySortsInsertedRowsLowToHigh)
{
    TemporaryDirectory directory("TemporaryTable.AscendingKeySortsInsertedRowsLowToHigh");
    EseInstance instance(directory);
    EseSession session(instance);

    JET_COLUMNID columnId = 0;
    auto temporaryTableId = OpenLongKeyTempTable(session,
                                                 JET_bitTTUpdatable,
                                                 &columnId);

    // Insert out of order; expect a sorted walk.
    InsertLong(session.Handle(), temporaryTableId, columnId, 30);
    InsertLong(session.Handle(), temporaryTableId, columnId, 10);
    InsertLong(session.Handle(), temporaryTableId, columnId, 20);

    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveFirst, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 10);
    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveNext, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 20);
    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveNext, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 30);

    CheckJet(JetCloseTable(session.Handle(), temporaryTableId));
}

EseIntegrationScenario(TemporaryTable, DescendingKeySortsInsertedRowsHighToLow)
{
    TemporaryDirectory directory(
        "TemporaryTable.DescendingKeySortsInsertedRowsHighToLow");
    EseInstance instance(directory);
    EseSession session(instance);

    JET_COLUMNID columnId = 0;
    auto temporaryTableId =
        OpenLongKeyTempTable(session,
                             JET_bitTTUpdatable,
                             &columnId,
                             JET_bitColumnTTKey | JET_bitColumnTTDescending);

    InsertLong(session.Handle(), temporaryTableId, columnId, 1);
    InsertLong(session.Handle(), temporaryTableId, columnId, 5);
    InsertLong(session.Handle(), temporaryTableId, columnId, 3);

    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveFirst, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 5);
    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveNext, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 3);
    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveNext, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnId) == 1);

    CheckJet(JetCloseTable(session.Handle(), temporaryTableId));
}

EseIntegrationScenario(TemporaryTable, ForwardOnlyRejectsKeyColumnsAtOpen)
{
    TemporaryDirectory directory("TemporaryTable.ForwardOnlyRejectsKeyColumnsAtOpen");
    EseInstance instance(directory);
    EseSession session(instance);

    // ForwardOnly cannot materialise a sort and so rejects columns
    // tagged JET_bitColumnTTKey at JetOpenTempTable time with
    // JET_errCannotMaterializeForwardOnlySort.
    JET_COLUMNDEF columnDefinitions[1] = { {} };
    columnDefinitions[0].cbStruct = sizeof(columnDefinitions[0]);
    columnDefinitions[0].coltyp = JET_coltypLong;
    columnDefinitions[0].grbit = JET_bitColumnTTKey;

    JET_COLUMNID columnIdentifiers[1] = { 0 };
    JET_TABLEID temporaryTableId = JET_tableidNil;
    RequireJetError(JetOpenTempTable(session.Handle(),
                                     columnDefinitions,
                                     1,
                                     JET_bitTTUpdatable | JET_bitTTForwardOnly,
                                     &temporaryTableId,
                                     columnIdentifiers),
                    JET_errCannotMaterializeForwardOnlySort);
}

EseIntegrationScenario(TemporaryTable, UpdatableTempTableAcceptsReplace)
{
    TemporaryDirectory directory("TemporaryTable.UpdatableTempTableAcceptsReplace");
    EseInstance instance(directory);
    EseSession session(instance);

    JET_COLUMNDEF columnDefinitions[2] = { {}, {} };
    columnDefinitions[0].cbStruct = sizeof(columnDefinitions[0]);
    columnDefinitions[0].coltyp = JET_coltypLong;
    columnDefinitions[0].grbit = JET_bitColumnTTKey;
    columnDefinitions[1].cbStruct = sizeof(columnDefinitions[1]);
    columnDefinitions[1].coltyp = JET_coltypLong;
    columnDefinitions[1].grbit = 0;

    JET_COLUMNID columnIdentifiers[2] = { 0, 0 };
    JET_TABLEID temporaryTableId = JET_tableidNil;
    CheckJet(JetOpenTempTable(session.Handle(),
                              columnDefinitions,
                              2,
                              JET_bitTTUpdatable,
                              &temporaryTableId,
                              columnIdentifiers));

    // Insert one row.
    {
        const int32_t keyValue = 1;
        const int32_t payload = 100;
        CheckJet(JetPrepareUpdate(session.Handle(), temporaryTableId, JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(), temporaryTableId,
                              columnIdentifiers[0],
                              &keyValue, sizeof(keyValue), 0, nullptr));
        CheckJet(JetSetColumn(session.Handle(), temporaryTableId,
                              columnIdentifiers[1],
                              &payload, sizeof(payload), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), temporaryTableId, nullptr, 0, nullptr));
    }

    // Replace the payload via JET_prepReplace on the temp table.
    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveFirst, 0));
    {
        const int32_t replacement = 999;
        CheckJet(JetPrepareUpdate(session.Handle(), temporaryTableId, JET_prepReplace));
        CheckJet(JetSetColumn(session.Handle(), temporaryTableId,
                              columnIdentifiers[1],
                              &replacement, sizeof(replacement), 0, nullptr));
        CheckJet(JetUpdate(session.Handle(), temporaryTableId, nullptr, 0, nullptr));
    }

    CheckJet(JetMove(session.Handle(), temporaryTableId, JET_MoveFirst, 0));
    Require(RetrieveCurrentLong(session.Handle(), temporaryTableId, columnIdentifiers[1])
            == 999);

    CheckJet(JetCloseTable(session.Handle(), temporaryTableId));
}
