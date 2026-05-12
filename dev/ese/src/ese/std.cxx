// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "std.hxx"

#ifndef ESE_OS_WINDOWS
//  On Windows, libese.dll's DllMain runs FOSPreinit() (the OS-layer
//  per-process preinit) on DLL_PROCESS_ATTACH and flips g_fDllUp = true.
//  Linux .so init has no DllMain equivalent, so we mirror that single
//  responsibility with a static-ctor: construct a COSLayerPreInit at
//  namespace scope so its ctor calls FOSPreinit() at .so-load time.
//
//  Anything else (ErrOSInit, ErrOSUInit, perfmon/tracing flags, JET param
//  defaults, ...) is the consumer's job — eseutil, BookStoreSample, the
//  test runner, etc. — exactly as on Windows.
namespace {

COSLayerPreInit g_oslayerLibeseInit;

}  //  namespace
#endif
