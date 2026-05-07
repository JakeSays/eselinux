// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// Layering-violation stubs and ODR-use definitions that the OS layer
// expects to link against but that don't have homes elsewhere on Linux.
//
// The Windows build keeps a parallel `oslite` static library so that the
// no-engine `eseutil` binary can link against trivial stubs (in
// `litent/violated.cxx`) while the full `oswinnt`-backed engine pulls in
// real bodies from upper layers. We don't currently split osposix into
// a lite variant — eseutil links against `libese.so` for the Jet API
// surface, which also drags in the real bodies of UtilReportEvent,
// JetErrorToString, FINSTSomeInitialized, and the OS Resource Manager
// lifecycle. So those bodies are NOT stubbed here; they would clash with
// the real engine implementations at libese.so link time.
//
// What stays here:
//   - UlParam: declared as inline in daedef.hxx, but several call sites
//     (notably error_posix.cxx's AssertFail path) take its address, so we
//     need a real symbol with the OS-layer (`INST = unsigned char`) shape.
//   - OSEdbg lifecycle: the Linux port intentionally drops edbg.cxx, so
//     no-op bodies match the documented port plan.
//   - CFastTraceLog version constants: trace.hxx declares them as
//     `const static ULONG ... = 2;` inside the class. C++17+ makes such
//     in-class const-init members inline; trace.cxx still ODR-uses them
//     through `LittleEndian<ULONG>::operator!=` (which takes a const& and
//     forces an address), so the header alone isn't enough.

#include "osstd.hxx"


////////////////////////////////////////////////
//  UlParam — OS-layer-shape stub. INST in this TU is `typedef unsigned
//  char INST;` (the engine-violation case has its own out-of-line body).

ULONG_PTR UlParam( const INST* const, const ULONG )
{
    return 0;
}


////////////////////////////////////////////////
//  OS Debugger Extension — intentionally dropped for the Linux port
//  (no edbg.cxx, no debugger-extension DLL on Linux). Stubs match the
//  oslite shape so os.cxx's lifecycle calls go through.

BOOL FOSEdbgPreinit( void )     { return fTrue; }
ERR  ErrOSEdbgInit( void )      { return JET_errSuccess; }
void OSEdbgTerm( void )         {}
void OSEdbgPostterm( void )     {}


////////////////////////////////////////////////
//  CFastTraceLog version constants — out-of-class definitions for ODR.
//  Values must match the in-class initializers in trace.hxx exactly.

const ULONG CFastTraceLog::ulFTLVersionMajor;
const ULONG CFastTraceLog::ulFTLVersionMinor;
const ULONG CFastTraceLog::ulFTLVersionUpdate;
