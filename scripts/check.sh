#!/usr/bin/env bash
# The full check (replaces the CI workflow; no GitHub Actions). Run via `agent-check`.
# Host tests (plain and ASan/UBSan), cppcheck, the discovery fixture gate, then
# every build env; the flash-size
# budget (scripts/check_flash_size.py) is enforced by each `pio run`.
set -euo pipefail
cd "$(dirname "$0")/.."

ENVS=(
  d1_mini
  ci-extended-frame
  ci-enhanced-resolution
  ci-ds18x20-sensor
  ci-ds18x20-troom
  ci-poweron-when-changing-mode
  ci-continue-without-mqtt
  ci-custom-payloads
  ci-ha-discovery
  ci-ha-discovery-outdoor
  ci-config-defaults
  ci-all-options
)
FIXTURES=(test/fixtures/discovery test/fixtures/discovery_all)

NEG_LOG="$(mktemp)"
trap 'rm -f "$NEG_LOG"' EXIT

step() { printf '\n==== %s ====\n' "$*"; }

step "drop committed discovery fixtures (so a stale one shows as a deletion)"
rm -f test/fixtures/discovery/*.txt test/fixtures/discovery_all/*.txt

step "pio test -e native"
pio test -e native

step "pio test -e native-san (AddressSanitizer + UBSan)"
pio test -e native-san

step "cppcheck over lib/mhi_pure and src"
# PROGMEM is an Arduino macro cppcheck cannot see. --inline-suppr honours the
# reasoned suppressions in the source; any other finding fails the check.
cppcheck --enable=warning,performance,portability --inline-suppr \
  --error-exitcode=1 -q --std=c++17 -DPROGMEM= -I lib/mhi_pure -I src \
  lib/mhi_pure src

step "discovery fixtures match the code"
git diff --exit-code -- "${FIXTURES[@]}"
test -z "$(git status --porcelain -- "${FIXTURES[@]}")"

# A checkout without the gitignored src/config_defaults.h has no OTA password,
# which src/support.h now refuses; opt out for the check's d1_mini build only.
D1_FLAGS=""
if [ ! -f src/config_defaults.h ]; then
  D1_FLAGS="-D ALLOW_EMPTY_OTA_PASSWORD"
fi

for e in "${ENVS[@]}"; do
  step "pio run -e $e"
  if [ "$e" = d1_mini ]; then
    PLATFORMIO_BUILD_FLAGS="$D1_FLAGS" pio run -e "$e"
  else
    pio run -e "$e"
  fi
done

step "an empty OTA_PASSWORD must not build (neg-empty-ota-password)"
if pio run -e neg-empty-ota-password >"$NEG_LOG" 2>&1; then
  cat "$NEG_LOG"
  echo "FAIL: a build with an empty OTA_PASSWORD succeeded; the guard in src/support.h is gone" >&2
  exit 1
fi
grep -q "OTA_PASSWORD is empty" "$NEG_LOG" || {
  cat "$NEG_LOG"
  echo "FAIL: neg-empty-ota-password failed, but not on the OTA_PASSWORD guard" >&2
  exit 1
}
echo "refused as expected"

printf '\nCHECK OK: native tests, fixture gate, %d env builds, OTA guard\n' "${#ENVS[@]}"
