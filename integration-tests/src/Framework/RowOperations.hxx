// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// Row-level convenience wrappers that bundle the JetPrepareUpdate /
// JetSetColumn / JetUpdate sequence for the common case of "insert a
// row whose only payload is a single fixed-size column". Scenarios
// that need the full DDL/DML surface go straight to the JET APIs;
// these helpers exist to keep coltyp-round-trip tests terse.

#pragma once

#include "Framework/Check.hxx"
#include "Framework/EseSession.hxx"
#include "Framework/EseTable.hxx"

#include <jetapi.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace ese::tests
{

// Insert a row containing one fixed-size column set to `value`.
// No transaction is opened — caller wraps with EseTransaction.
template <typename ValueType>
void InsertSingleFixedColumnRow(EseTable& table,
                                JET_COLUMNID columnId,
                                ValueType value)
{
    auto sessionHandle = table.Database().Session().Handle();
    CheckJet(JetPrepareUpdate(sessionHandle, table.Id(), JET_prepInsert));
    CheckJet(JetSetColumn(sessionHandle,
                          table.Id(),
                          columnId,
                          &value,
                          sizeof(value),
                          0,
                          nullptr));
    CheckJet(JetUpdate(sessionHandle, table.Id(), nullptr, 0, nullptr));
}

// Insert a row containing one variable-size column set to the bytes in
// `payload`.
inline void InsertSingleVariableColumnRow(EseTable& table,
                                          JET_COLUMNID columnId,
                                          const void* payload,
                                          uint32_t payloadBytes)
{
    auto sessionHandle = table.Database().Session().Handle();
    CheckJet(JetPrepareUpdate(sessionHandle, table.Id(), JET_prepInsert));
    CheckJet(JetSetColumn(sessionHandle,
                          table.Id(),
                          columnId,
                          payload,
                          payloadBytes,
                          0,
                          nullptr));
    CheckJet(JetUpdate(sessionHandle, table.Id(), nullptr, 0, nullptr));
}

// Retrieve a fixed-size column from the current record. Returns the
// value; throws if the column comes back NULL or the wrong size.
template <typename ValueType>
ValueType RetrieveFixedColumnFromCurrentRecord(EseTable& table,
                                               JET_COLUMNID columnId)
{
    ValueType result = {};
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(table.Database().Session().Handle(),
                               table.Id(),
                               columnId,
                               &result,
                               sizeof(result),
                               &actualBytes,
                               0,
                               nullptr));
    Require(actualBytes == sizeof(result));
    return result;
}

// Retrieve a variable-size column into a freshly-allocated vector.
inline std::vector<uint8_t> RetrieveVariableColumnFromCurrentRecord(EseTable& table,
                                                                    JET_COLUMNID columnId,
                                                                    uint32_t maximumBytes)
{
    std::vector<uint8_t> buffer(maximumBytes);
    uint32_t actualBytes = 0;
    CheckJet(JetRetrieveColumn(table.Database().Session().Handle(),
                               table.Id(),
                               columnId,
                               buffer.data(),
                               static_cast<uint32_t>(buffer.size()),
                               &actualBytes,
                               0,
                               nullptr));
    buffer.resize(actualBytes);
    return buffer;
}

} // namespace ese::tests
