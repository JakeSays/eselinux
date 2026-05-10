// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "std.hxx"

#ifndef ESE_OS_WINDOWS
//  On Windows the engine's OS layer comes up via DllMain when libese.dll is
//  loaded. Linux .so init runs through C++ static constructors instead;
//  give libese.so a process-lifetime COSLayerPreInit so any binary that
//  links against it (BookStoreSample, future Jet API consumers) gets the
//  OS layer pre-initialized before main() runs. eseutil still creates its
//  own COSLayerPreInit in wmain — that one becomes a no-op because the
//  ctor checks g_fDllUp.
namespace {
COSLayerPreInit g_oslayerLibeseInit;
}
#endif
