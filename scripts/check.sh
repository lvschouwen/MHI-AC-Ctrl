#!/usr/bin/env bash
# The full check (replaces the CI workflow; no GitHub Actions). Run via `agent-check`.
# Host tests, the discovery fixture gate, then every build env; the flash-size
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

step() { printf '\n==== %s ====\n' "$*"; }

step "drop committed discovery fixtures (so a stale one shows as a deletion)"
rm -f test/fixtures/discovery/*.txt test/fixtures/discovery_all/*.txt

step "pio test -e native"
pio test -e native

step "discovery fixtures match the code"
git diff --exit-code -- "${FIXTURES[@]}"
test -z "$(git status --porcelain -- "${FIXTURES[@]}")"

for e in "${ENVS[@]}"; do
  step "pio run -e $e"
  pio run -e "$e"
done

printf '\nCHECK OK: native tests, fixture gate, %d env builds\n' "${#ENVS[@]}"
