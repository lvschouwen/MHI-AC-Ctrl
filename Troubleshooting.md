# Troubleshooting
Problems and solutions collected by users. Please read this before opening a new issue.

If you can't find the solution for your problem here and not in the existing  [Issues](https://github.com/absalom-muc/MHI-AC-Ctrl/issues?q=is%3Aissue) you can open a new issue. But please consider the following topics:
- You should have a basic understanding how to use an ESP8266. That includes the usage of an IDE for programming, compile, flash, OTA and log output via the serial terminal.
- You should have a basic understanding of the MQTT protocol and how to setup a MQTT broker

There are great descriptions in the WWW related to the topics above. Please use your preferred search engine.

If you open a new issue, please consider the following topics:
- Use a use a meaningful title, so that the next user with a similar problem can recognize it
- Which program version do you use?
- Add the exact name of your AC indoor unit, e.g. SRK 35 ZS-S
- MHI-AC-Ctrl outputs some basic status information via the serial terminal. Please use it for your first analysis and upload it together with a new Issue. Check this [section](#recording-a-basic-log-file)

## Recording a basic log file
You can use `pio device monitor` or any serial terminal to record the serial output. Apply a baud rate of 115200 Baud and switch **on** the time stamp. In order not to overload the Issue, the log should not be copied directly into the Issue, but copied to a text file and attached to the Issue.

## Recording a detailed log of the SPI waveforms
This is done via the [SPI-logger](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/testprog/SPI_logger.ino). You can use `pio device monitor` or any serial terminal to record the serial output. Apply a baud rate of 115200 Baud and switch **off** the time stamp. In order not to overload the Issue, the log should not be copied directly into the Issue, but copied to a text file and attached to the Issue.

## Known limitations
MHI-AC-Ctrl doesn't support all functions of the infrared remote control. This is because some functions are not reflected by the SPI payload (or I'm not aware of the according SPI codes):
- ECO, Silent and Night set back mode

This should be considered especially when you use the IR RC in parallel to MHI-AC-Ctrl.

To find out which bytes a remote function changes on your unit, see [Finding out what a remote button does](SW-Configuration.md#finding-out-what-a-remote-button-does): `diag/frame` names the status bytes that change, `diag/opdata` shows unknown operating data with its value, and `set/OpDataRequest` asks the AC for any operating-data code.

Connecting the MHI-AC-Ctrl controller will **disable** the RC timer functionality. This also apply to the standard MHI WiFi (or other) controller. See [#148](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/148) for more information and [Passive Mode](SW-Configuration.md#passive-mode) for a workaround.

## :fire: ESP8266 crashes periodically
The `ResetReason` topic says how the last start came about, and `Uptime` shows how long ago that was. `Hardware Watchdog`, `Software Watchdog` and `Exception` are crashes; `Power On`, `Software/System restart` (an OTA flash, `set/reset`) and `External System` are not. A unit that keeps reporting a crash reason with a short uptime is the case below. Three crashes in a row, each within 120 s of the boot, start [safe mode](#fire-the-unit-is-unavailable-for-10-minutes-safe-mode). `abort()`, `panic()`, a failed `assert` or `new` and a stack overflow show as `Software/System restart`, but safe mode counts them as crashes too.

For a periodic crash there are different causes possible:

### :fire: Pins not properly connected
During boot the frequency of the SPI pins are checked. This is very helpful to identify SPI signal connection problems. Following values are expected:
- SCK frequency > 3000 Hz
- MOSI frequency < SCK frequency (usually ~ 500 Hz)
- MISO frequency ~ 0 Hz (usually 0Hz)

If you see other values, please re-check
- the connection between the MHI-AC-Ctrl board and your AC
- that there is no short between the three signals

If MISO frequency>10Hz, something other than MHI-AC-Ctrl is driving the MISO line, which should be an output during normal operation. The fault is published on the `Wiring` topic and MISO is left as an input, so the board never drives against that signal. The unit stays on WiFi and MQTT, still reports the AC status and stays reachable over OTA. It cannot send commands, though, so after about 120 seconds the AC goes into its error state (see [below](#fire-ac-switches-power-off-sometimes)). Fix the wiring, then restart with `set/reset` or a power cycle to run the check again. Up to v2.8 the program stopped and rebooted in a loop instead, which also ruled out OTA.

Typical faults seen in the past:
- SCK frequency = 0Hz => SCK pin not connected
- SCK frequency < MOSI frequency => SCK and MOSI pins swapped
- MISO frequency = SCK frequency => MISO and SCK pins shorted

Typical multimeter readings for SCK and MOSI: ~4.7V on the level shifter's HV pins (and X1), ~3V on the LV pins (and D5/D7).

### :fire: You use PubSubClient v2.8.0
There was a [bug](https://github.com/knolleary/pubsubclient/issues/747) introduced in PubSubClient version 2.8.0. Please use [PubSubClient3](https://github.com/hmueller01/pubsubclient3) instead, or downgrade to PubSubClient v2.7.0.

## :fire: MQTT connects / disconnects periodically
The HOSTNAME specified in support.h is used as WiFi hostname, MQTT hostname and OTA hostname. In case that you use more than one MHI-AC-Ctrl, e.g. in a multi-split configuration, you have to use unique HOSTNAME to every PCB.

## :fire: Unit stopped joining WiFi after a router change
A unit that ran for years goes off WiFi after a router firmware update or channel change, and stays off; the serial log shows a wrong-password status (`WL_WRONG_PASSWORD`, 6) although the password is right, or the board looks dead. This is [#224](https://github.com/absalom-muc/MHI-AC-Ctrl/issues/224): a router with 802.11ax (WiFi 6) enabled on 2.4 GHz refusing the ESP8266's default 802.11n join. Since the [PHY mode fallback](SW-Configuration.md#wifi-phy-mode-fallback) the firmware tries 802.11g on its own after five minutes without a link, so give it ten minutes before reaching for a USB cable; a unit that got in that way reports `11g` on the `WIFI_PHY` topic. On older firmware, add `WiFi.setPhyMode(WIFI_PHY_MODE_11G);` to `initWiFi()` and flash over USB.

If the credentials themselves are wrong (a new password or SSID), a build with `RESCUE_AP_PASSWORD` opens its [rescue access point](SW-Configuration.md#rescue-access-point) after 15 minutes without a link, for 10 minutes at a time:
1. build an image with the corrected `WIFI_SSID`/`WIFI_PASSWORD`;
2. join the Wi-Fi network `<HOSTNAME>-rescue` with `RESCUE_AP_PASSWORD` (the unit is at 192.168.4.1; while you are connected the access point stays up);
3. upload: `python3 ~/.platformio/packages/framework-arduinoespressif8266/tools/espota.py -i 192.168.4.1 -p 8266 -a <OTA_PASSWORD> -f .pio/build/d1_mini/firmware.bin`;
4. the unit restarts on the new image and joins the network. If the upload does not start, the unit may have closed the access point: wait for it to come back.

## :fire: AC switches power off sometimes
When there is for >=120 seconds no valid MISO frame (e.g. >=120 s without WiFi or MQTT, since the SPI loop then isn't served), the AC goes into an error state (MQTT topic Errorcode=1) and switches off, presumably as a safety function. Leave the error state with a command via IR-RC or SPI; switching on again via SPI needs the Power On command.

## :fire: Receiving the AC status works, but can't change values
Different root causes are possible:
### :fire: MISO pin not connected to AC
Please check this [section](#fire-pins-not-properly-connected)

### :fire: Wrong MQTT set path used
The MQTT path for receiving the status is different from the MQTT path for setting values. Re-check that you use the set-path described in [SW-Configuration.md](https://github.com/absalom-muc/MHI-AC-Ctrl/blob/master/SW-Configuration.md#mqtt-status). Please pay attention to the case sensitivity.
## :fire: Vanes up/down after a change on the IR remote
v2.8 published `?` on `Vanes` after the IR remote was used, because the set flags a write over SPI adds were missing. The position itself is in the frame: on an SRK20ZS-WF every remote vane press showed up in `DB1 & 0x30` (swing in `DB0 & 0x40`), so `Vanes` now always shows the setting (fork #38). The `Remote` topic says whether the remote made the last change.

## :fire: Log shows errors, but it works nevertheless
With some ACs the SPI connection is fragile because of a different timing. E.g.
```
mhi_ac_ctrl_core.loop error: -4
```

Possible error codes:
```
err_msg_invalid_signature = -1
err_msg_invalid_checksum = -2
err_msg_timeout_SCK_low = -3
err_msg_timeout_SCK_high = -4
```

However, this does not appear to be critical and is usually not noticed by the user.

## :fire: Room temperature is toggling
This effect occurs with some AC models. The cause is unclear.

## :fire: The unit is unavailable for 10 minutes: safe mode
After three crashes in a row, each within 120 s of the boot before, the unit starts in [crash-loop safe mode](SW-Configuration.md#crash-loop-safe-mode): Wi-Fi and OTA only, for 10 minutes. You recognise it by:
- Home Assistant shows the unit unavailable (`connected` 0) for about 10 minutes, while the AC still works on its remote;
- the serial log starts with `Safe mode check: reset reason 2, crashes in a row 3, ...` (reason 4 when the last crash was an `abort()`, a failed `assert` or a stack overflow) and then `SAFE MODE: three crashes in a row; Wi-Fi and OTA only, a normal boot in 10 min`;
- the unit still answers OTA: it is on the network as `<hostname>._arduino._tcp` (`avahi-browse -rt _arduino._tcp` lists it);
- once it boots normally again, the retained `SafeMode` topic reads 1 or more, and `ResetReason` reads `Software/System restart`.

Use the 10 minutes to flash a build that works, as below in [OTA cannot find the device](#fire-ota-cannot-find-the-device). If you do nothing, the unit boots normally after 10 minutes, and a fault that is still there brings it back to safe mode after three more crashes.

If safe mode comes back after every normal boot while the build is known to be good, look for a retained `set/reset` (`mosquitto_sub -h <broker> -v -W 2 -t <MQTT_PREFIX>set/reset` prints it) and clear it: `mosquitto_pub -h <broker> -r -n -t <MQTT_PREFIX>set/reset`.

## :fire: OTA cannot find the device
The Arduino IDE is no longer used for this project. Flash over OTA with `pio run -t upload --upload-port <hostname>.local`, as described in the [README](README.md#building); that runs [espota.py](https://github.com/esp8266/Arduino/blob/master/tools/espota.py) underneath. If the hostname does not resolve, pass the unit's IP address instead.
