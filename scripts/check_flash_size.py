"""Fail the build when the firmware image outgrows its budget.

Later phases of the improvement programme add WiFiManager and Home Assistant
discovery, both of which are hungry. There is no spare ESP8266: if the image
quietly grows past what the OTA partition can take, the first symptom is a
device that refuses updates, and recovering it means opening an air
conditioner. So growth is a build failure here, not a surprise on the bench.

The budget is set per environment in platformio.ini:

    custom_max_firmware_bytes = 460000

Omit the option to disable the check.
"""

import os

Import("env")  # noqa: F821 -- injected by SCons


def _budget(env):
    raw = env.GetProjectOption("custom_max_firmware_bytes", None)
    if raw is None or str(raw).strip() == "":
        return None
    try:
        return int(str(raw).strip())
    except ValueError:
        print("check_flash_size: custom_max_firmware_bytes is not an integer: %r" % raw)
        env.Exit(1)


def check_flash_size(source, target, env):
    budget = _budget(env)
    if budget is None:
        return

    firmware = target[0].get_abspath()
    if not os.path.exists(firmware):
        return
    size = os.path.getsize(firmware)
    headroom = budget - size
    percent = 100.0 * size / budget

    print(
        "Flash budget: %d of %d bytes used (%.1f%%), %d bytes headroom"
        % (size, budget, percent, headroom)
    )

    if size > budget:
        print("")
        print("=" * 72)
        print("FLASH BUDGET EXCEEDED")
        print("  firmware : %d bytes" % size)
        print("  budget   : %d bytes" % budget)
        print("  over by  : %d bytes" % -headroom)
        print("")
        print("Shrink the image, or raise custom_max_firmware_bytes in")
        print("platformio.ini deliberately -- after checking it still fits the")
        print("OTA partition, because OTA is the only recovery path.")
        print("=" * 72)
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_flash_size)  # noqa: F821
