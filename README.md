# MHI-AC-Ctrl
Reads and writes data (e.g. power, mode, fan status etc.) from/to a Mitsubishi Heavy Industries (MHI) air conditioner (AC) via SPI controlled by MQTT. The AC is the SPI master and the ESP8266 is the SPI slave.

<img src="/images/IoT-MQTT-Panel.jpg" width=200 align="right" />

# Attention:
:warning: You have to open the indoor unit to have access to the SPI. Opening of the indoor unit should be done by 
a qualified professional because faulty handling may cause leakage of water, electric shock or fire! :warning: 

# Prerequisites:
For use of the program you have to connect your ESP8266 (I use a LOLIN(WEMOS) D1 R2 & mini at 160 MHz) via a
cable connector to your air conditioner. This has to be a split device (separated indoor and outdoor unit).
With several indoor units on one outdoor unit, the units elect one of them to publish the outdoor unit's values and its Home Assistant device; see `GROUP_ROOT` in [SW-Configuration.md](SW-Configuration.md#several-indoor-units-on-one-outdoor-unit).
I assume that all AC units of the type "SRK xx ZS-S" / "SRC xx ZS-S" are supported. I use the indoor unit SRK 35 ZS-S and the outdoor unit SRC 35 ZS-S. Users reported that additionally the following models are supported:

- [SRF xx ZJX-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issue-632187165)
- [SRF xx ZMX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-704789297)
- [SRF xx ZMXA-S](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/91)
- SRF xx ZF-W
- [SRF xx ZS-W](https://github.com/absalom-muc/MHI-AC-Ctrl/pull/216#issue-3234769567) works with frame size 20
- [SRK xx ZJ-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/10)
- [SRK xx ZM-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/9)
- [SRK xx ZJX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-646469621)
- [SRK xx ZJX-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-646968940)
- [SRK xx ZR-WF](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/214)
- [SRK xx ZRA-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-730628655) / DXK xx ZRA-W
- [SRK xx ZSA-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-891649495) / DXK xx ZSA-W
- [SRK63ZSPR-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/149)
- SRK xx ZSXA-W/A
- [SRK xx ZSX-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/6#issuecomment-582242372)
- [SRK xx ZSX-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/17#issuecomment-643748095)
- [SRK xx ZS-W](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/121)
- [SRK xx ZS-WF](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/121#issuecomment-1938350129)
- [SRC 35Z SA-W]
- [DXK xx ZMA-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/201)
- [SRK 2824 T (Japanese domestic model)](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/211)

Unsupported models:
- [SRK71ZEA-S1](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/143)
- [SRK35ZC-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/154)
- [SRK xx ZSPR-S](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/200) (except SRK63ZSPR-S)

 
If you find out that also other models are supported that are not listed here, please give feedback so that I can expand the list. In general, please recheck if your AC has a CNS connector before you spend time and money to build the hardware.

# Installing:

## Hardware:
The ESP8266 is powered from the AC via DC-DC (12V -> 5V) converter. 
The ESP8266 SPI signals SCL (SPI clock), MOSI (Master Out Slave In) and MISO (Master In Slave Out) are connected via a voltage level shifter 5V <-> 3.3V with the AC. Direct connection of the signals without a level shifter could damage your ESP8266!
More details are described in [Hardware.md](Hardware.md).

## Software:
The program uses the following libraries:
 - MQTT client library [PubSubClient3](https://github.com/hmueller01/pubsubclient3)
   - :warning: Don't use PubSubClient v2.8.0, it is broken due to issue [#747](https://github.com/knolleary/pubsubclient/issues/747) :warning:
 - [ArduinoOTA](https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA) (might be removed in future)
 
and optionally you need for the use of an external temperature sensor DS18x20 the libraries
 - [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html)
 - [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library)

### Building

This fork is a [PlatformIO](https://platformio.org/) project. The exact library
versions are pinned in [platformio.ini](platformio.ini), so you do not install
them by hand and everyone builds against the same code. The CPU frequency is
set to 160 MHz there as well, which the AC's SPI timing is happier with.

```sh
pio run                 # build the firmware for a Wemos D1 mini
pio run -t upload       # flash over USB
pio run -t upload -e d1_mini --upload-port <device>.local   # flash over OTA
pio test -e native      # run the host tests, no hardware needed
pio test -e native-san  # the same tests under AddressSanitizer and UBSan
scripts/check.sh        # the full gate: tests, sanitizers, cppcheck, all 12 builds
```

`scripts/check.sh` is what gates a push (there is no GitHub Actions). It needs
`cppcheck` on the PATH next to PlatformIO 6.2.0. Its findings were triaged with
cppcheck 2.19.0; another version may report different ones.

**OTA password is required.** An empty `OTA_PASSWORD` would let anyone on your
network flash the unit, so a normal build refuses it at compile time. Set
`OTA_PASSWORD` in `src/config_defaults.h` (below). Only the `ci-*` environments,
which are compile-only, define `ALLOW_EMPTY_OTA_PASSWORD` to skip the check.

Your WiFi and MQTT settings go in `src/config_defaults.h`, which is in
`.gitignore` and so never ends up in a commit. Anything you do not set there
falls back to the defaults in [support.h](src/support.h). All configuration
options are described in [SW-Configuration.md](SW-Configuration.md).

```c
// src/config_defaults.h
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MQTT_SERVER   "192.168.1.10"
#define HOSTNAME      "MHI-AC-Ctrl-livingroom"
#define OTA_PASSWORD  "choose-a-password"
```

The Arduino IDE is no longer supported: it needs the `.ino` sketch layout, and
that layout is what prevented the project from being built automatically.
`scripts/check.sh` compiles the whole `#ifdef` matrix (the `ci-*` environments
in [platformio.ini](platformio.ini)) and runs the host tests before a push.

### Bench tools (`testprog/`)

`testprog/` holds two single-file Arduino sketches for looking at the AC's SPI
signals. They are not part of the firmware build (`pio run` ignores them) and
nothing tests them; use them on a bench board, not on a unit in service.

- `SPI_logger.ino` logs the SCK, MOSI and MISO edges on D5/D7/D6 as a `.vcd`
  waveform that PulseView and similar viewers open. Serial 115200 baud, no
  timestamps.
- `frequency_measurement.ino` counts the SCK falling edges on D5 for 1 s per
  pass, repeatedly, to check the clock frequency. Serial 115200 baud, timestamps on.

To build one with PlatformIO, copy it to `src/main.cpp` of a scratch project
with `platform = espressif8266`, `board = d1_mini`, `framework = arduino`
(add `#include <Arduino.h>` at the top, as a `.ino` gets it implicitly), then
`pio run -t upload` and `pio device monitor -b 115200`. In the Arduino IDE,
open the `.ino` directly (ESP8266 board package, Wemos D1 mini).

In a previous version (see [here](https://github.com/absalom-muc/MHI-AC-SPY)) I used the Hardware-SPI of the ESP8266. But since the SPI documentation of ESP8266 is poor, I decided to switch to a Software based SPI.
This Software based SPI is reliable and the performance of the ESP8266 is sufficient for this use case.
In case of problems please check the [Troubleshooting guide](Troubleshooting.md).

# Enhancement
If you are interested to have a deeper look on the SPI protocol or want to trace the SPI signals, please check [MHI-AC-Trace](https://github.com/absalom-muc/MHI-AC-Trace). But this is not needed for the standard user of MHI-AC-Ctrl.

# License
This project is licensed under the MIT License - see the LICENSE.md file for details

# Acknowledgments
The coding of the [SPI protocol](https://github.com/absalom-muc/MHI-AC-Trace/blob/main/SPI.md) of the AC is a nightmare. Without [rjdekker's MHI2MQTT](https://github.com/rjdekker/MHI2MQTT) I had no chance to understand the protocol! Unfortunately rjdekker is no longer active on GitHub. He used an Arduino plus an ESP8266 for his project.
Also thank you very much on the authors and contributors of [MQTT client](https://github.com/knolleary/pubsubclient), [ArduinoOTA](https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA), [OneWire](https://www.pjrc.com/teensy/td_libs_OneWire.html) and [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) libraries.

Last but not least thank you for the implementation of MHI-AC-Ctrl in [different environments](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/SW-Configuration.md#integration-examples) (FHEM, Tasmota, Home Assistant etc.)
