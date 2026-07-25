#include "mhi_mqtt.h"

size_t mhi_copy_payload(char* dst, size_t dst_size, const uint8_t* payload, size_t length) {
  if (dst == NULL || dst_size == 0) return 0;

  size_t copied = length;
  if (copied > dst_size - 1) copied = dst_size - 1;
  for (size_t i = 0; i < copied; i++) dst[i] = (char)payload[i];
  dst[copied] = '\0';
  return copied;
}
