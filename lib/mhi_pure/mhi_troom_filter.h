// The Troom publish filter.
//
// The AC's internal sensor moves by a quarter degree several times a minute;
// TROOM_FILTER_LIMIT holds a change back until it exceeds that limit. The
// filter used to be a function-static in main.cpp that nothing could reset,
// so after an MQTT reconnect the core re-sent every status and this one
// swallowed Troom. It lives here with a reset so the reconnect path can clear
// it, and so the threshold semantics are pinned by a test.
//
// Pure logic, no Arduino.

#pragma once

#include <stdint.h>

struct MhiTroomFilter {
  uint8_t last;   // the last value that passed
  bool has_last;  // false after a reset: the next value always passes
};

void mhi_troom_filter_reset(MhiTroomFilter* filter);

// Whether `value` (an MHI Troom byte, 0.25 degC steps) should be published.
// The first value after a reset always passes. After that a value passes when
// it differs from the last passed value by more than limit_celsius, so with
// the default 0.25 a single step is held back and two steps go out. A value
// that passes becomes the new reference; one that is held back does not.
bool mhi_troom_filter_pass(MhiTroomFilter* filter, uint8_t value, float limit_celsius);
