// Bounded handling of MQTT payloads.
//
// An incoming payload is a byte range inside the MQTT client's receive buffer,
// not a C string: it has no terminator, and the bytes after it belong to
// whatever the broker sent next. Terminating it in place writes into that
// buffer, and for a message that fills it, one byte past the end.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Copy `length` payload bytes into `dst` and NUL-terminate. Truncates rather
// than overflowing, and writes nothing at all when dst_size is 0. Returns the
// number of bytes actually copied, which is less than `length` on truncation.
size_t mhi_copy_payload(char* dst, size_t dst_size, const uint8_t* payload, size_t length);
