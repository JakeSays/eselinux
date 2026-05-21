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

#include <optional>

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

EseIntegrationScenario(MultiValue, SetUniqueMultiValuesRejectsDuplicateInSameRow)
{
    TemporaryDirectory directory(
        "MultiValue.SetUniqueMultiValuesRejectsDuplicateInSameRow");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "MultiValue.mdb");
    EseTable table(database, "InRowUnique");

    auto columnId = table.AddColumn("Tag",
                                    JET_coltypLong,
                                    JET_bitColumnTagged | JET_bitColumnMultiValued);

    static constexpr int32_t ExistingValue = 42;
    static constexpr int32_t DistinctValue = 99;

    // Seed itag 1 with ExistingValue, then attempt to add a second
    // itag carrying the same value but with JET_bitSetUniqueMultiValues.
    // The engine must refuse the duplicate with
    // JET_errMultiValuedDuplicate.  Adding a DistinctValue under the
    // same flag must succeed.
    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));

        // itag 1 = ExistingValue (no flag yet — establishes the existing
        // multi-value to compare against).
        JET_SETINFO firstSetInformation = {};
        firstSetInformation.cbStruct = sizeof(firstSetInformation);
        firstSetInformation.itagSequence = 1;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                              &ExistingValue, sizeof(ExistingValue),
                              0, &firstSetInformation));

        // itag 0 (engine picks the next free itag) = ExistingValue
        // with the uniqueness flag — must be rejected.
        JET_SETINFO duplicateSetInformation = {};
        duplicateSetInformation.cbStruct = sizeof(duplicateSetInformation);
        duplicateSetInformation.itagSequence = 0;
        RequireJetError(JetSetColumn(session.Handle(), table.Id(), columnId,
                                     &ExistingValue, sizeof(ExistingValue),
                                     JET_bitSetUniqueMultiValues,
                                     &duplicateSetInformation),
                        JET_errMultiValuedDuplicate);

        // A DistinctValue under the same flag must succeed and land
        // in a new itag slot.
        JET_SETINFO acceptedSetInformation = {};
        acceptedSetInformation.cbStruct = sizeof(acceptedSetInformation);
        acceptedSetInformation.itagSequence = 0;
        CheckJet(JetSetColumn(session.Handle(), table.Id(), columnId,
                              &DistinctValue, sizeof(DistinctValue),
                              JET_bitSetUniqueMultiValues,
                              &acceptedSetInformation));

        CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
        transaction.Commit();
    }

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));
    Require(RetrieveItag(table, columnId, 1) == ExistingValue);

    // Scan up through itag 4 — exactly one slot beyond 1 must hold
    // DistinctValue, every other slot must be unset (the rejected
    // duplicate never landed in the record).
    auto retrieveItagOptional = [&](uint32_t itag) -> std::optional<int32_t>
    {
        JET_RETINFO retrieveInformation = {};
        retrieveInformation.cbStruct = sizeof(retrieveInformation);
        retrieveInformation.itagSequence = itag;
        int32_t value = 0;
        uint32_t actualBytes = 0;
        const auto result = JetRetrieveColumn(session.Handle(), table.Id(),
                                              columnId,
                                              &value, sizeof(value),
                                              &actualBytes,
                                              0, &retrieveInformation);
        if (result == JET_wrnColumnNull)
        {
            return std::nullopt;
        }
        CheckJet(result);
        Require(actualBytes == sizeof(value));
        return value;
    };

    uint32_t distinctSightings = 0;
    for (uint32_t itag = 2; itag <= 4; ++itag)
    {
        const auto observed = retrieveItagOptional(itag);
        if (observed.has_value())
        {
            Require(*observed == DistinctValue);
            ++distinctSightings;
        }
    }
    Require(distinctSightings == 1);
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
