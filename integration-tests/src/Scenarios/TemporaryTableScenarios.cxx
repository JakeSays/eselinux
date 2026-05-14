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

EseIntegrationScenario(TemporaryTable, OpenTempTable3SortsViaUnicodeIndex)
{
    TemporaryDirectory directory(
        "TemporaryTable.OpenTempTable3SortsViaUnicodeIndex");
    EseInstance instance(directory);
    EseSession session(instance);

    //  JetOpenTempTable3 replaces JetOpenTempTable2's bare lcid arg
    //  with a JET_UNICODEINDEX pointer.  That lets callers pass NLS
    //  flags (case-insensitive, etc.) alongside the locale.
    //
    //  Build a temp table sorted by a Unicode-text key with the
    //  English (US) LCID and LCMAP_LOWERCASE so the sort folds
    //  case.  Insert "banana", "Apple", "cherry" and walk forward —
    //  case-insensitive sort puts Apple first.
    JET_COLUMNDEF columns[1] = { {} };
    columns[0].cbStruct = sizeof(columns[0]);
    columns[0].coltyp = JET_coltypLongText;
    columns[0].cp = 1200;  //  UTF-16
    columns[0].grbit = JET_bitColumnTTKey;

    JET_UNICODEINDEX unicodeIndex = {};
    unicodeIndex.lcid = 1033;  // en-US
    //  ErrNORMCheckLCMapFlags (`dev/ese/src/os/norm.cxx:685`) validates
    //  these against a whitelist that includes NORM_IGNORECASE and
    //  IGNORENONSPACE; it auto-ORs in LCMAP_SORTKEY.  Anything else
    //  is rejected with JET_errInvalidLCMapStringFlags.
    unicodeIndex.dwMapFlags = 0x00000001 /* NORM_IGNORECASE */;

    JET_COLUMNID columnId = 0;
    JET_TABLEID tableid = JET_tableidNil;
    CheckJet(JetOpenTempTable3(session.Handle(),
                               columns,
                               1,
                               &unicodeIndex,
                               JET_bitTTUpdatable,
                               &tableid,
                               &columnId));
    Require(tableid != JET_tableidNil);
    Require(columnId != 0);

    auto insertWide = [&](const char16_t* text, uint32_t cch) {
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  tableid,
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              tableid,
                              columnId,
                              text,
                              cch * sizeof(char16_t),
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           tableid,
                           nullptr,
                           0,
                           nullptr));
    };

    //  JET_bitTTUpdatable temp tables don't have the implicit
    //  transaction semantics the read-only variants enjoy; wrap
    //  inserts in an explicit transaction so JetSetColumn doesn't
    //  trip JET_errNotInTransaction.
    CheckJet(JetBeginTransaction(session.Handle()));
    static const char16_t Banana[] = u"banana";
    static const char16_t Apple[]  = u"Apple";
    static const char16_t Cherry[] = u"cherry";
    insertWide(Banana, 6);
    insertWide(Apple, 5);
    insertWide(Cherry, 6);
    CheckJet(JetCommitTransaction(session.Handle(), 0));

    CheckJet(JetMove(session.Handle(), tableid, JET_MoveFirst, 0));
    char16_t buffer[16] = {};
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               tableid,
                               columnId,
                               buffer,
                               sizeof(buffer),
                               &cbActual,
                               0,
                               nullptr));
    //  Case-insensitive sort puts "Apple" first.
    Require(buffer[0] == u'A' || buffer[0] == u'a');

    CheckJet(JetCloseTable(session.Handle(), tableid));
}
