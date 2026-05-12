// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"

#include <format>
#include <string>
#include <unordered_map>

namespace ese::tests
{

namespace
{

// Hand-maintained table of the JET errors we expect tests to interact
// with. The engine has ~600 codes; we only need pretty-printing for
// the ones our scenarios reference. Anything not in the table prints
// as "JET_err<N>" — still actionable, just less self-documenting.
const std::unordered_map<JET_ERR, std::string_view>& JetErrorNameTable()
{
    static const std::unordered_map<JET_ERR, std::string_view> table =
    {
        { JET_errSuccess, "JET_errSuccess" },
        { JET_errInvalidParameter, "JET_errInvalidParameter" },
        { JET_errInvalidGrbit, "JET_errInvalidGrbit" },
        { JET_errOutOfMemory, "JET_errOutOfMemory" },
        { JET_errInstanceUnavailable, "JET_errInstanceUnavailable" },
        { JET_errInvalidName, "JET_errInvalidName" },
        { JET_errInvalidObject, "JET_errInvalidObject" },
        { JET_errInvalidBufferSize, "JET_errInvalidBufferSize" },
        { JET_errInvalidColumnType, "JET_errInvalidColumnType" },
        { JET_errAlreadyInitialized, "JET_errAlreadyInitialized" },
        { JET_errNotInitialized, "JET_errNotInitialized" },
        { JET_errFileNotFound, "JET_errFileNotFound" },
        { JET_errDatabaseInUse, "JET_errDatabaseInUse" },
        { JET_errDatabaseDuplicate, "JET_errDatabaseDuplicate" },
        { JET_errDatabaseNotFound, "JET_errDatabaseNotFound" },
        { JET_errDatabaseInvalidName, "JET_errDatabaseInvalidName" },
        { JET_errTableLocked, "JET_errTableLocked" },
        { JET_errTableDuplicate, "JET_errTableDuplicate" },
        { JET_errObjectNotFound, "JET_errObjectNotFound" },
        { JET_errColumnNotFound, "JET_errColumnNotFound" },
        { JET_errColumnDuplicate, "JET_errColumnDuplicate" },
        { JET_errIndexDuplicate, "JET_errIndexDuplicate" },
        { JET_errIndexNotFound, "JET_errIndexNotFound" },
        { JET_errRecordNotFound, "JET_errRecordNotFound" },
        { JET_errKeyDuplicate, "JET_errKeyDuplicate" },
        { JET_errNoCurrentRecord, "JET_errNoCurrentRecord" },
        { JET_errWriteConflict, "JET_errWriteConflict" },
        { JET_errTransTooDeep, "JET_errTransTooDeep" },
        { JET_errOutOfSessions, "JET_errOutOfSessions" },
        { JET_errOutOfCursors, "JET_errOutOfCursors" },
        { JET_errOutOfBuffers, "JET_errOutOfBuffers" },
        { JET_errOutOfAutoincrementValues, "JET_errOutOfAutoincrementValues" },
        { JET_errSessionInUse, "JET_errSessionInUse" },
        { JET_errSectorSizeNotSupported, "JET_errSectorSizeNotSupported" },
        { JET_errInternalError, "JET_errInternalError" },
        { JET_wrnColumnNull, "JET_wrnColumnNull" },
        { JET_wrnBufferTruncated, "JET_wrnBufferTruncated" },
        { JET_wrnTableEmpty, "JET_wrnTableEmpty" },
        { JET_wrnSeekNotEqual, "JET_wrnSeekNotEqual" },
    };
    return table;
}

} // namespace

std::string JetErrorName(JET_ERR errorCode)
{
    const auto& table = JetErrorNameTable();
    auto iterator = table.find(errorCode);
    if (iterator != table.end())
    {
        return std::string(iterator->second);
    }
    return std::format("JET_err<{}>", static_cast<int>(errorCode));
}

namespace internal
{

void RaiseJetFailure(JET_ERR errorCode,
                     std::string_view expressionText,
                     std::source_location location)
{
    throw ScenarioFailure(std::format("{}:{}: {} returned {} ({})",
                                      location.file_name(),
                                      location.line(),
                                      expressionText,
                                      JetErrorName(errorCode),
                                      static_cast<int>(errorCode)));
}

void RaiseRequireFailure(std::string_view conditionText,
                         std::source_location location)
{
    throw ScenarioFailure(std::format("{}:{}: Require({}) failed",
                                      location.file_name(),
                                      location.line(),
                                      conditionText));
}

void RaiseJetErrorMismatch(JET_ERR actualErrorCode,
                           JET_ERR expectedErrorCode,
                           std::string_view expressionText,
                           std::source_location location)
{
    throw ScenarioFailure(std::format(
        "{}:{}: {} returned {} ({}), expected {} ({})",
        location.file_name(),
        location.line(),
        expressionText,
        JetErrorName(actualErrorCode),
        static_cast<int>(actualErrorCode),
        JetErrorName(expectedErrorCode),
        static_cast<int>(expectedErrorCode)));
}

} // namespace internal

} // namespace ese::tests
