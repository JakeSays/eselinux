// Intentionally empty. The canonical platform.h lives in
// dev/ese/published/inc/platform.h and is included by cc.hxx, which every
// ESE translation unit reaches. This stub exists so that #include "platform.h"
// directives issued from within the windows-shim directory resolve cleanly
// without the compiler trying to re-enter itself.
#pragma once
