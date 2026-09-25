#include "mhi_temp.h"

// Troom byte = degC * 4 + 61
static const int kTroomOffset = 61;
static const float kTroomStepsPerDegree = 4.0f;

// The DS18x20 reports in 1/128 degC; 32 raw units is one 0.25 degC step.
static const int kDs18x20RawPerTroomStep = 32;
static const int16_t kDs18x20RawMax = 48 * 128;
static const int16_t kDs18x20RawMin = -10 * 128;

uint8_t mhi_troom_round_from_celsius(float celsius) {
  const float encoded = celsius * kTroomStepsPerDegree + (float)kTroomOffset + 0.5f;
  if (!(encoded >= 0.0f)) return 0;  // NaN too
  if (encoded >= 256.0f) return 255;
  return (uint8_t)encoded;  // non-negative, so the cast floors
}

uint8_t mhi_troom_from_celsius(float celsius) {
  // Truncation happens after the offset is added, as in the original
  // (byte)(f * 4 + 61); doing it before would shift negative temperatures.
  const int encoded = (int)(celsius * kTroomStepsPerDegree + (float)kTroomOffset);
  if (encoded < 0) return 0;
  if (encoded > 255) return 255;
  return (uint8_t)encoded;
}

float mhi_celsius_from_troom(int troom) {
  return (troom - kTroomOffset) / kTroomStepsPerDegree;
}

bool mhi_troom_celsius_plausible(float celsius) {
  return celsius > -10.0f && celsius < 48.0f;
}

bool mhi_troom_byte_plausible(uint8_t troom) {
  return mhi_troom_celsius_plausible(mhi_celsius_from_troom(troom));
}

bool mhi_ds18x20_raw_plausible(int16_t raw) {
  return raw <= kDs18x20RawMax && raw >= kDs18x20RawMin;
}

uint8_t mhi_troom_from_ds18x20_raw(int16_t raw) {
  // Bias by the offset before dividing so the truncation lands on the final
  // value, the way (int)(celsius * 4 + 61) does. Dividing first and adding
  // afterwards would round sub-zero readings the other way and put the two
  // encodings one step apart.
  const int biased = raw + kTroomOffset * kDs18x20RawPerTroomStep;
  if (biased < 0) return 0;
  const int encoded = biased / kDs18x20RawPerTroomStep;
  if (encoded > 255) return 255;
  return (uint8_t)encoded;
}
