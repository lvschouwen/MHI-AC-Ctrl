// The one place that reads the gitignored src/config_defaults.h. support.h and
// MHI-AC-Ctrl.h both include this ahead of their #ifndef-guarded defaults, so a
// user's file is read once per translation unit, whatever it contains.
#pragma once

// MHI_SKIP_CONFIG_DEFAULTS (build flag, only the neg-* check env) makes the file
// invisible, so a guard test cannot be overridden by a local config_defaults.h.
#ifndef MHI_SKIP_CONFIG_DEFAULTS
#if defined(__has_include)
#if __has_include("config_defaults.h")
#include "config_defaults.h"
#endif
#endif
#endif
