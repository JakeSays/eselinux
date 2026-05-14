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

#include <cstdint>
#include <cstring>
#include <vector>

using namespace ese::tests;


//  Returns true when the engine reports AES-256 is available on this
//  platform/runtime.  False on:
//    - libsodium absent (dlopen fails)
//    - libsodium <= 1.0.18 on aarch64 (no AArch64 AES path in that
//      version of the library)
//    - any future build/runtime where ErrOSEncryptionInit'd
//      g_fEncryptionAvailable came back false
//
//  Scenarios that need encryption call this first and return early
//  (counts as scenario PASS) when it returns false — the test
//  framework has no native "skip" status, so a graceful early return
//  is the closest we can get without spuriously failing on boxes
//  without crypto.
static bool IsAesEncryptionAvailable()
{
    uint32_t cbKey = 0;
    const auto err = JetCreateEncryptionKey(JET_EncryptionAlgorithmAes256,
                                            nullptr,
                                            0,
                                            &cbKey);
    return err == JET_errBufferTooSmall;
}


//  Helper: pull a key blob out of JetCreateEncryptionKey using its
//  buffer-too-small protocol.  Caller must have already confirmed
//  IsAesEncryptionAvailable() before invoking — Require()s here will
//  trip otherwise.
static std::vector<uint8_t> CreateAes256Key()
{
    uint32_t cbKey = 0;
    auto err = JetCreateEncryptionKey(JET_EncryptionAlgorithmAes256,
                                      nullptr,
                                      0,
                                      &cbKey);
    Require(err == JET_errBufferTooSmall);
    Require(cbKey > 0);

    std::vector<uint8_t> key(cbKey);
    CheckJet(JetCreateEncryptionKey(JET_EncryptionAlgorithmAes256,
                                    key.data(),
                                    cbKey,
                                    &cbKey));
    Require(cbKey == key.size());
    return key;
}


EseIntegrationScenario(Encryption, CreateEncryptionKeyRoundTripsBufferTooSmall)
{
    if (!IsAesEncryptionAvailable())
    {
        return;
    }

    //  JetCreateEncryptionKey's discovery protocol: pass cbKey=0 and
    //  the engine reports the required size; allocate, call again,
    //  get the populated blob.
    auto key = CreateAes256Key();
    Require(!key.empty());

    //  Second call with the same byte count should also produce a
    //  valid (different-bits, random) key.
    auto key2 = CreateAes256Key();
    Require(key2.size() == key.size());
    Require(std::memcmp(key.data(), key2.data(), key.size()) != 0);
}


EseIntegrationScenario(Encryption, ColumnRoundTripsThroughEncryptedTable)
{
    if (!IsAesEncryptionAvailable())
    {
        return;
    }

    TemporaryDirectory directory(
        "Encryption.ColumnRoundTripsThroughEncryptedTable");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "EncryptedColumn.mdb");
    EseTable table(database, "Secret");

    //  The Secret column is declared with JET_bitColumnEncrypted at
    //  create time — that marks it persistently as needing the
    //  encrypt/decrypt round trip whenever the row is written or
    //  read.  No special grbit is required on SetColumn /
    //  RetrieveColumn; the engine routes through encrypt_posix.cxx
    //  transparently as long as a table-level key is wired.
    const auto plainColumn =
        table.AddColumn("Plain", JET_coltypLong, JET_bitColumnNotNULL);
    const auto secretColumn =
        table.AddColumn("Secret",
                        JET_coltypLongBinary,
                        JET_bitColumnEncrypted);

    auto key = CreateAes256Key();
    CheckJet(JetSetTableInfoA(session.Handle(),
                              table.Id(),
                              key.data(),
                              static_cast<uint32_t>(key.size()),
                              JET_TblInfoEncryptionKey));

    static constexpr int32_t kPlainValue = 0xABCDEF01;
    static constexpr char kSecretValue[] = "the password is 12345";
    const uint32_t cbSecret = static_cast<uint32_t>(sizeof(kSecretValue));

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              plainColumn,
                              &kPlainValue,
                              sizeof(kPlainValue),
                              0,
                              nullptr));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              secretColumn,
                              kSecretValue,
                              cbSecret,
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    //  Read both columns back; the encrypted column gets decrypted
    //  via the wired key.
    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    int32_t plainBack = 0;
    uint32_t cbActual = 0;
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               plainColumn,
                               &plainBack,
                               sizeof(plainBack),
                               &cbActual,
                               0,
                               nullptr));
    Require(cbActual == sizeof(plainBack));
    Require(plainBack == kPlainValue);

    char secretBack[sizeof(kSecretValue)] = {};
    CheckJet(JetRetrieveColumn(session.Handle(),
                               table.Id(),
                               secretColumn,
                               secretBack,
                               sizeof(secretBack),
                               &cbActual,
                               0,
                               nullptr));
    Require(cbActual == cbSecret);
    Require(std::memcmp(secretBack, kSecretValue, cbSecret) == 0);
}


EseIntegrationScenario(Encryption, WrongKeyFailsDecryption)
{
    if (!IsAesEncryptionAvailable())
    {
        return;
    }

    TemporaryDirectory directory("Encryption.WrongKeyFailsDecryption");
    EseInstance instance(directory);
    EseSession session(instance);
    EseDatabase database(session, "EncryptedColumn.mdb");
    EseTable table(database, "Secret");

    const auto secretColumn =
        table.AddColumn("Secret",
                        JET_coltypLongBinary,
                        JET_bitColumnEncrypted);

    //  Write a row with key A.
    auto keyA = CreateAes256Key();
    CheckJet(JetSetTableInfoA(session.Handle(),
                              table.Id(),
                              keyA.data(),
                              static_cast<uint32_t>(keyA.size()),
                              JET_TblInfoEncryptionKey));

    static constexpr char kSecret[] = "secret payload";
    const uint32_t cbSecret = static_cast<uint32_t>(sizeof(kSecret));

    {
        EseTransaction transaction(session);
        CheckJet(JetPrepareUpdate(session.Handle(),
                                  table.Id(),
                                  JET_prepInsert));
        CheckJet(JetSetColumn(session.Handle(),
                              table.Id(),
                              secretColumn,
                              kSecret,
                              cbSecret,
                              0,
                              nullptr));
        CheckJet(JetUpdate(session.Handle(),
                           table.Id(),
                           nullptr,
                           0,
                           nullptr));
        transaction.Commit();
    }

    //  Swap to a different (random, valid-format) key and try to read
    //  — GCM's auth tag mismatches, engine surfaces
    //  JET_errDecryptionFailed (a signal to the caller that the key
    //  doesn't match or the bytes were tampered with).
    auto keyB = CreateAes256Key();
    CheckJet(JetSetTableInfoA(session.Handle(),
                              table.Id(),
                              keyB.data(),
                              static_cast<uint32_t>(keyB.size()),
                              JET_TblInfoEncryptionKey));

    CheckJet(JetMove(session.Handle(), table.Id(), JET_MoveFirst, 0));

    char scratch[sizeof(kSecret)] = {};
    uint32_t cbActual = 0;
    const auto err = JetRetrieveColumn(session.Handle(),
                                       table.Id(),
                                       secretColumn,
                                       scratch,
                                       sizeof(scratch),
                                       &cbActual,
                                       0,
                                       nullptr);
    Require(err == JET_errDecryptionFailed);
}
