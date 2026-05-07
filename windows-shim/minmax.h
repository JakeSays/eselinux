// Linux shim for <minmax.h>. Intentionally empty: osstd_.hxx pulls in
// <algorithm> + `using namespace std` immediately after this header, and
// engine call sites resolve `min(a,b)` to std::min. Defining min/max as
// Win32-style macros breaks libstdc++ STL headers that reference min/max
// in template bodies. (MSVC's STL brackets its template defs with
// push/pop_macro to survive that trick; libstdc++ doesn't.) Engine sites
// where `min(a,b)` arguments have mismatched types need explicit casts.
#pragma once
