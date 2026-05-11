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
//   EseLibWithTestsRunner                       # run tier-1 ('*')
//   EseLibWithTestsRunner CFlushMap.*           # tier-1 subset
//   EseLibWithTestsRunner -?                    # list registered tests
//   EseLibWithTestsRunner -d <dir>              # tier-2: spin up JET, run '*'
//   EseLibWithTestsRunner -d <dir> KVPStore.*   # tier-2 subset

#include <cstdio>
#include <cstring>

extern "C" int RunJetUnitTestsForRunner( const char* szPattern, const char* szDbDir );

int main( int argc, char** argv )
{
    const char* szPattern = "*";
    const char* szDbDir = nullptr;
    for ( int i = 1; i < argc; ++i )
    {
        if ( std::strcmp( argv[ i ], "-d" ) == 0 && i + 1 < argc )
        {
            szDbDir = argv[ ++i ];
        }
        else
        {
            szPattern = argv[ i ];
        }
    }
    const int failures = RunJetUnitTestsForRunner( szPattern, szDbDir );
    std::fprintf( stderr, "\n=== %d failure(s) ===\n", failures );
    return failures == 0 ? 0 : 1;
}
