# MHI-AC-Ctrl (fork)

ESP8266 firmware (PlatformIO) that bridges Mitsubishi Heavy air conditioners to MQTT and Home Assistant. Fork of absalom-muc/MHI-AC-Ctrl (upstream dormant since 2025-12); origin is lvschouwen/MHI-AC-Ctrl. Plain `gh` resolves to upstream: ALWAYS use `gh -R lvschouwen/MHI-AC-Ctrl`.

## Toolchain
- `platform = espressif8266@4.2.1`; libs PubSubClient3@3.3.1, OneWire@2.3.8, DallasTemperature@4.0.6 (see platformio.ini). Never use knolleary/PubSubClient.

## Full check
- `agent-check` runs `scripts/check.sh`: `pio test -e native` (never `pio run -e native`), the discovery fixture gate (regenerated fixtures must match git), then `pio run -e <env>` for the 12 envs (d1_mini + ci-*), each held to the 460000 B OTA budget by `scripts/check_flash_size.py`. No GitHub Actions; this script is the gate.
- Pins: `platformio==6.2.0` and `espressif8266@<version>` in platformio.ini. Bump on purpose, in their own commit.
- Pure logic lives in lib/mhi_pure with a Unity test in test/test_mhi_*; firmware glue is verified on the bench.

## Hardware
- No spare board; OTA is the only recovery. Never flash or change the live unit's config without a per-step go from Lucas.
- One flash per build: fold all flash-needing items into one build.

## HA / MQTT contract
- The `airco/<room>/{Mode,Fan,Tsetpoint,Vanes,Troom,Action,connected,...}` topics and the HA discovery payloads are consumed (and tested) by the hass-config repo.
- Topic names, payload keys and discovery ids are append-only. A rename needs a hass-config issue first.
- Before a flash-needing change, send hass-config the discovery render.

## Git
- Issue first, commits straight to master. Public repo: no IPs, hostnames, SSIDs, MACs, room-to-IP maps or tokens anywhere.
- Standing knowledge lives in memory; open work lives in GitHub issues.
- Agent runs follow ~/.claude/rules/common/agent-runs.md (slots `.claude/worktrees/wt-N`, settings `.claude/agent-wt.conf`). Opus for anything flash-adjacent or protocol-level.
- Implementers stop at a green `agent-check` and a LOCAL commit; flashing, OTA and pushing are main-session steps.
