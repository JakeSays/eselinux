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

#include <cstring>

EseIntegrationScenario(LongValue, ShortLongTextRoundTrip)
{
    ese::tests::TemporaryDirectory directory("LongValue.ShortLongTextRoundTrip");
    ese::tests::EseInstance        instance(directory);
    ese::tests::EseSession         session(instance);
    ese::tests::EseDatabase        database(session, "LongValue.mdb");
    ese::tests::EseTable           table(database, "Documents");

    JET_COLUMNDEF columnDefinition = {};
    columnDefinition.cbStruct      = sizeof(columnDefinition);
    columnDefinition.coltyp        = JET_coltypLongText;
    columnDefinition.cp            = 1252;

    JET_COLUMNID bodyColumnId = 0;
    CheckJet(JetAddColumnA(session.Handle(),
                           table.Id(),
                           "Body",
                           &columnDefinition,
                           nullptr,
                           0,
                           &bodyColumnId));

    static constexpr const char* WrittenValue = "Hello, ESE long value.";
    const auto writtenLength = static_cast<uint32_t>(std::strlen(WrittenValue));

    ese::tests::EseTransaction transaction(session);
    CheckJet(JetPrepareUpdate(session.Handle(), table.Id(), JET_prepInsert));
    CheckJet(JetSetColumn(session.Handle(),
                          table.Id(),
                          bodyColumnId,
                          WrittenValue,
                          writtenLength,
                          0,
                          nullptr));
    CheckJet(JetUpdate(session.Handle(), table.Id(), nullptr, 0, nullptr));
    transaction.Commit();

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    char     readBuffer[64] = { 0 };
    uint32_t readLength     = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               bodyColumnId,
                               readBuffer,
                               sizeof(readBuffer),
                               &readLength,
                               0,
                               nullptr));
    Require(readLength == writtenLength);
    Require(std::memcmp(readBuffer, WrittenValue, writtenLength) == 0);
}
