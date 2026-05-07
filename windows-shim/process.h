// Linux shim for <process.h>. The MSVC CRT exposes _beginthreadex,
// _endthreadex, _getpid here. ESE includes it in dllentry.cxx and
// thread.cxx but doesn't actually call those entry points outside
// Windows-specific code paths — this file is intentionally near-empty.
//
// If a future port phase needs _beginthreadex, the typical posix mapping
// is: pthread_create() returning the thread handle in HANDLE form.
#pragma once
