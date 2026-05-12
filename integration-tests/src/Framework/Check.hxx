// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
//  Assertion + JET-error-translation helpers.
//
//  CheckJet(expr)       — calls a JET API; if err < 0, throws ScenarioFailure
//                         carrying the err name and call site.  Warnings
//                         (err > 0) are logged but allowed.
//  Require(condition)   — asserts a C++ predicate; throws on failure.
//  RequireJetError(expr, expectedErr)
//                       — asserts the JET call returned exactly the named
//                         error.  Used by the Error matrix scenarios.

#pragma once

#include <jet.h>

#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ese::tests
{

class ScenarioFailure : public std::runtime_error
{
public:
    explicit ScenarioFailure(std::string message)
        : std::runtime_error(std::move(message))
    {
    }
};

//  Return the canonical name of a JET_ERR (e.g. "JET_errRecordNotFound").
//  Falls back to "JET_err<N>" for codes we don't have a name for.
std::string JetErrorName(JET_ERR errorCode);

//  Implementation helpers used by the macros below.
namespace internal
{

void RaiseJetFailure(JET_ERR errorCode,
                     std::string_view expressionText,
                     std::source_location location);

void RaiseRequireFailure(std::string_view conditionText,
                         std::source_location location);

void RaiseJetErrorMismatch(JET_ERR actualErrorCode,
                           JET_ERR expectedErrorCode,
                           std::string_view expressionText,
                           std::source_location location);

}  //  namespace internal

}  //  namespace ese::tests

//  Macros — these are macros (rather than functions) so the expression
//  text and source_location capture the actual call site, not the
//  helper's body.

#define CheckJet(expression)                                                   \
    do                                                                         \
    {                                                                          \
        const JET_ERR _checkJetErrorCode = (expression);                       \
        if (_checkJetErrorCode < JET_errSuccess)                               \
        {                                                                      \
            ::ese::tests::internal::RaiseJetFailure(_checkJetErrorCode,        \
                                                   #expression,                \
                                                   std::source_location::current()); \
        }                                                                      \
    } while (0)

#define Require(condition)                                                     \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            ::ese::tests::internal::RaiseRequireFailure(#condition,            \
                                                       std::source_location::current()); \
        }                                                                      \
    } while (0)

#define RequireJetError(expression, expectedErrorCode)                         \
    do                                                                         \
    {                                                                          \
        const JET_ERR _checkJetErrorCode = (expression);                       \
        if (_checkJetErrorCode != (expectedErrorCode))                         \
        {                                                                      \
            ::ese::tests::internal::RaiseJetErrorMismatch(_checkJetErrorCode,  \
                                                         (expectedErrorCode),  \
                                                         #expression,          \
                                                         std::source_location::current()); \
        }                                                                      \
    } while (0)
