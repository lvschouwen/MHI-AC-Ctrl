#include "mhi_phy.h"

void mhi_phy_link_up(MhiPhyFallback* f, uint8_t mode, uint32_t now_ms) {
  f->link_seen_ms = now_ms;
  f->link_mode = mode;
}

uint8_t mhi_phy_mode_for_join(const MhiPhyFallback* f, uint32_t now_ms, uint32_t period_ms) {
  // Unsigned subtraction, so the millis() wrap after 49.7 days is harmless.
  const uint32_t periods_without_link = (now_ms - f->link_seen_ms) / period_ms;
  if (periods_without_link % 2 == 0) return f->link_mode;
  return f->link_mode == MHI_PHY_11G ? MHI_PHY_11N : MHI_PHY_11G;
}

const char* mhi_phy_mode_text(uint8_t mode) {
  switch (mode) {
    case MHI_PHY_11B: return "11b";
    case MHI_PHY_11G: return "11g";
    case MHI_PHY_11N: return "11n";
    default: return "?";
  }
}
