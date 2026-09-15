#include "mhi_diag.h"

// The AC clocks at a few kHz; anything slower means we are not seeing it.
static const uint32_t kSckMinHz = 3000;
// MOSI carries the AC's frames, so it is active but necessarily slower than
// the clock it is sampled with.
static const uint32_t kMosiMinHz = 30;
// MISO is our output. Edges on it during the measurement mean something else
// is driving the line.
static const uint32_t kMisoMaxHz = 10;

uint8_t mhi_wiring_faults(uint32_t sck_hz, uint32_t mosi_hz, uint32_t miso_hz) {
  uint8_t faults = 0;
  if (sck_hz <= kSckMinHz) faults |= MHI_WIRING_FAULT_SCK;
  if (mosi_hz <= kMosiMinHz || mosi_hz >= sck_hz) faults |= MHI_WIRING_FAULT_MOSI;
  if (miso_hz > kMisoMaxHz) faults |= MHI_WIRING_FAULT_MISO;
  return faults;
}

void mhi_wiring_fault_text(uint8_t faults, char* out, size_t out_size) {
  if (out_size == 0) return;

  static const char* const kNames[] = {"SCK", "MOSI", "MISO"};
  static const uint8_t kBits[] = {MHI_WIRING_FAULT_SCK, MHI_WIRING_FAULT_MOSI,
                                  MHI_WIRING_FAULT_MISO};

  size_t written = 0;
  for (size_t i = 0; i < sizeof(kBits) / sizeof(kBits[0]); i++) {
    if (!(faults & kBits[i])) continue;
    if (written > 0 && written + 1 < out_size) out[written++] = ',';
    for (const char* c = kNames[i]; *c && written + 1 < out_size; c++) out[written++] = *c;
  }

  if (written == 0) {
    for (const char* c = "o.k."; *c && written + 1 < out_size; c++) out[written++] = *c;
  }
  out[written] = '\0';
}

bool mhi_miso_may_be_driven(uint8_t faults) {
  return (faults & MHI_WIRING_FAULT_MISO) == 0;
}
