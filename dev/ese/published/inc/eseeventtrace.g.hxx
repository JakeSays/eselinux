// Linux port stub for the generated eseeventtrace.g.hxx. On Windows,
// this file is generated from ese.man (the ETW provider manifest) by
// the message compiler / mc.exe. The Linux port disables ETW for v1
// (see project memory: "DISABLE_EVENT_LOG for v1"); a future LTTng
// integration replaces this stub with a generated equivalent.
//
// Until then, every event-trace probe collapses to a constant `false`
// so the engine builds and runs without an ETW provider.
#pragma once

// FOSEventTraceEnabled is declared (without a primary body) in
// oseventtrace.hxx; supply a generic always-false definition so
// downstream explicit instantiations have a body to bind to.
template< OSEventTraceGUID etguid >
INLINE BOOL FOSEventTraceEnabled() { return fFalse; }
