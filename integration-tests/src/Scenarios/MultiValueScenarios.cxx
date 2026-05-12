// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"
#include "Framework/EseDatabase.hxx"
#include "Framework/EseInstance.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"
#include "Framework/EseTransaction.hxx"
#include "Framework/Scenario.hxx"
#include "Framework/TemporaryDirectory.hxx"

using namespace ese::tests;

namespace
{

// Retrieve one value from a multi-valued column by itag.
int32_t RetrieveItag(EseTable& table, JET_COLUMNID columnId, uint32_t itagSequence)
{
    JET_RETINFO retrieveInformation = {};
    retrieveInformation.cbStruct = sizeof(retrieveInformation);
    retrieveInformation.itagSequence = itagSequence;

    int32_t value = 0;
    uint32_t actualSize = 0;
    CheckJet(JetRetrieveColumn(table.Database().Session().Handle(),
                               table.Id(),
                               columnId,
                               &value,
                               sizeof(value),
                               &actualSize,
                               0,
                               &retrieveInformation));
    return value;
}

// Set one value into a multi-valued column at a specific itag.
void SetItag(EseTable& table,
             JET_COLUMNID columnId,
             uint32_t itagSequence,
             int32_t value)
{
    JET_SETINFO setInformation = {};
    setInformation.cbStruct = sizeof(setInformation);
    setInformation.itagSequence = itagSequence;
    CheckJet(JetSetColumn(table.Database().Session().Handle(),
                          table.Id(),
                          columnId,
                          &value,
                          sizeof(value),
                          0,
                          &setInformation));
}

}  // namespace

EseIntegrationScenario(MultiValue, TwoItagsRoundTrip)
{
    TemporaryDirectory directory("MultiValue.TwoItagsRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "Tagged");

    auto columnId = table.AddColumn("Tags",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 101);
        SetItag(table, columnId, 2, 202);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveItag(table, columnId, 1) == 101);
    Require(RetrieveItag(table, columnId, 2) == 202);
}

EseIntegrationScenario(MultiValue, ManyItagsRoundTrip)
{
    TemporaryDirectory directory("MultiValue.ManyItagsRoundTrip");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "Many");

    auto columnId = table.AddColumn("Many",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    static constexpr int ItagCount = 50;

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        for (int index = 1; index <= ItagCount; ++index)
        {
            SetItag(table, columnId, static_cast<uint32_t>(index), index * 10);
        }
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    for (int index = 1; index <= ItagCount; ++index)
    {
        Require(RetrieveItag(table, columnId, static_cast<uint32_t>(index)) == index * 10);
    }
}

EseIntegrationScenario(MultiValue, RetrievingPastLastItagReturnsColumnNull)
{
    TemporaryDirectory directory("MultiValue.RetrievingPastLastItagReturnsColumnNull");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "PastLast");

    auto columnId = table.AddColumn("Tags",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 11);
        SetItag(table, columnId, 2, 22);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    // Itag 3 was never set — retrieve must signal NULL via the
    // JET_wrnColumnNull warning code.
    JET_RETINFO retrieveInformation = {};
    retrieveInformation.cbStruct = sizeof(retrieveInformation);
    retrieveInformation.itagSequence = 3;

    int32_t scratch = 0;
    uint32_t actualSize = 0;
    const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                  table.Id(),
                                                  columnId,
                                                  &scratch,
                                                  sizeof(scratch),
                                                  &actualSize,
                                                  0,
                                                  &retrieveInformation);
    Require(retrieveResult == JET_wrnColumnNull);
}

EseIntegrationScenario(MultiValue, ReplaceSingleItagPreservesOthers)
{
    TemporaryDirectory directory("MultiValue.ReplaceSingleItagPreservesOthers");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "Mutable");

    auto columnId = table.AddColumn("Tags",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 100);
        SetItag(table, columnId, 2, 200);
        SetItag(table, columnId, 3, 300);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    {
        EseTransaction transaction(session);
        CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepReplace));
        SetItag(table, columnId, 2, 999);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveItag(table, columnId, 1) == 100);
    Require(RetrieveItag(table, columnId, 2) == 999);
    Require(RetrieveItag(table, columnId, 3) == 300);
}

EseIntegrationScenario(MultiValue, RetrievingMissingItagReturnsColumnNotFound)
{
    TemporaryDirectory directory(
        "MultiValue.RetrievingMissingItagReturnsColumnNotFound");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "Sparse");

    auto columnId = table.AddColumn("Tags",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 42);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    JET_RETINFO retrieveInformation = {};
    retrieveInformation.cbStruct = sizeof(retrieveInformation);
    retrieveInformation.itagSequence = 5;     // not set

    int32_t scratch = 0;
    uint32_t actualSize = 0;
    const auto retrieveResult = JetRetrieveColumn(session.Handle(),
                                                  table.Id(),
                                                  columnId,
                                                  &scratch,
                                                  sizeof(scratch),
                                                  &actualSize,
                                                  0,
                                                  &retrieveInformation);
    Require(retrieveResult == JET_wrnColumnNull);
}

EseIntegrationScenario(MultiValue, UniqueMultiValueIndexRejectsDuplicateAcrossRows)
{
    TemporaryDirectory directory(
        "MultiValue.UniqueMultiValueIndexRejectsDuplicateAcrossRows");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "UniqueTags");

    auto columnId = table.AddColumn("Tag",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    static constexpr std::string_view UniqueKey =
        std::string_view("+Tag\0\0", 6);
    table.CreateIndex("ByTagUnique",
                      UniqueKey,
                      JET_bitIndexUnique | JET_bitIndexIgnoreNull);

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 7);
        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    // Inserting a second row with the same value must violate the
    // unique index.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
        SetItag(table, columnId, 1, 7);
        RequireJetError(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr),
                        JET_errKeyDuplicate);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepCancel));
        transaction.Commit();
    }
}
