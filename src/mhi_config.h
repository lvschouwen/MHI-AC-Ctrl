// The one place that reads the gitignored src/config_defaults.h. support.h and
// MHI-AC-Ctrl.h both include this ahead of their #ifndef-guarded defaults, so a
// user's file is read once per translation unit, whatever it contains.
#pragma once

#if defined(__has_include)
#if __has_include("config_defaults.h")
#include "config_defaults.h"
#endif
#endif
