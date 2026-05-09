// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// Linux runner for the JETUNITTEST suites compiled into eselibwithtests.
// Mirrors the role of blue/'s test harness on Windows: invoke
// JetUnitTest::RunTests with a name pattern and report pass/fail.
// Engine-context code (the JetUnitTest call) lives in
// runner_entrypoint.cxx alongside the engine PCH; this TU stays free of
// engine includes so it doesn't have to pull esestd.hxx.
//
// Usage:
//   EseLibWithTestsRunner                 # run all (matches '*')
//   EseLibWithTestsRunner CFlushMap.*     # run a wildcard subset
//   EseLibWithTestsRunner CPAGE.PgftGet*  # run a single suite
//   EseLibWithTestsRunner -?              # print all registered tests

#include <cstdio>

extern "C" int RunJetUnitTestsForRunner( const char* szPattern );

int main( int argc, char** argv )
{
    const char* szPattern = ( argc >= 2 ) ? argv[1] : "*";
    const int failures = RunJetUnitTestsForRunner( szPattern );
    std::fprintf( stderr, "\n=== %d failure(s) ===\n", failures );
    return failures == 0 ? 0 : 1;
}
