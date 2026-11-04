# MHI-AC-Ctrl (fork)

ESP8266 firmware (PlatformIO) that bridges Mitsubishi Heavy air conditioners to MQTT and Home Assistant. Fork of absalom-muc/MHI-AC-Ctrl (upstream dormant since 2025-12); origin is lvschouwen/MHI-AC-Ctrl. Plain `gh` resolves to upstream: ALWAYS use `gh -R lvschouwen/MHI-AC-Ctrl`.

## Toolchain
- `platform = espressif8266@4.2.1`; libs PubSubClient3@3.3.1, OneWire@2.3.8, DallasTemperature@4.0.6 (see platformio.ini). Never use knolleary/PubSubClient.

## Full check (what CI does)
- `pio test -e native` (never `pio run -e native`)
- fixture gate: `rm -f test/fixtures/discovery/*.txt test/fixtures/discovery_all/*.txt`, rerun the tests, then `git diff --exit-code -- test/fixtures/discovery test/fixtures/discovery_all` (commit regenerated fixtures)
- `pio run -e <env>` for each of the 12 envs in .github/workflows/ci.yml (d1_mini + ci-*); `scripts/check_flash_size.py` enforces the 460000 B OTA budget on each
- Pins: `platformio==<version>` in both CI install steps and `espressif8266@<version>` in platformio.ini. Bump them together, on purpose, in their own commit; CI is the gate.
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
- Implementers (subagents) stop at a green `pio test -e native` plus the env builds and a LOCAL commit; flashing, OTA and `git push` are main-session steps.
