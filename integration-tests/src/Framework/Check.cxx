// Copyright (c) Jake Helfert
// Licensed under the MIT License.

#include "Framework/Check.hxx"

#include <format>
#include <string>

namespace ese::tests
{

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
