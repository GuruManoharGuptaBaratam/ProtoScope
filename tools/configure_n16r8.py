#!/usr/bin/env python3
"""Migrate an existing sdkconfig to the user-confirmed N16R8 memory settings."""

import argparse
import re
from datetime import datetime
from pathlib import Path


def n16r8_config(source):
    settings = {
        "CONFIG_ESPTOOLPY_FLASHSIZE": '"16MB"',
        "CONFIG_SPIRAM": "y",
        "CONFIG_SPIRAM_MODE_QUAD": "n",
        "CONFIG_SPIRAM_MODE_OCT": "y",
        "CONFIG_SPIRAM_TYPE_AUTO": "y",
        "CONFIG_SPIRAM_TYPE_ESPPSRAM16": "n",
        "CONFIG_SPIRAM_TYPE_ESPPSRAM32": "n",
        "CONFIG_SPIRAM_TYPE_ESPPSRAM64": "n",
        "CONFIG_SPIRAM_SPEED_120M": "n",
        "CONFIG_SPIRAM_SPEED_80M": "n",
        "CONFIG_SPIRAM_SPEED_40M": "y",
        "CONFIG_SPIRAM_SPEED": "40",
        "CONFIG_SPIRAM_BOOT_HW_INIT": "y",
        "CONFIG_SPIRAM_BOOT_INIT": "y",
        "CONFIG_SPIRAM_MEMTEST": "y",
        "CONFIG_SPIRAM_IGNORE_NOTFOUND": "n",
        "CONFIG_SPIRAM_ECC_ENABLE": "n",
    }
    for size in (1, 2, 4, 8, 16, 32, 64, 128):
        settings[f"CONFIG_ESPTOOLPY_FLASHSIZE_{size}MB"] = "y" if size == 16 else "n"
    lines = []
    for line in source.splitlines():
        match = re.match(r"(?:# )?(CONFIG_\w+)(?:=| is not set)", line)
        if match is None or match[1] not in settings:
            lines.append(line)
    for name, value in settings.items():
        lines.append(f"# {name} is not set" if value == "n" else f"{name}={value}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdkconfig", nargs="?", type=Path,
                        default=Path(__file__).resolve().parent.parent / "sdkconfig")
    args = parser.parse_args()
    if not args.sdkconfig.exists():
        print("No generated sdkconfig yet; sdkconfig.defaults already selects N16R8.")
        return
    original = args.sdkconfig.read_text()
    updated = n16r8_config(original)
    if original == updated:
        print("N16R8 memory settings already selected.")
        return
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    backup = args.sdkconfig.with_name(f"{args.sdkconfig.name}.before-n16r8-{stamp}")
    backup.write_text(original)
    args.sdkconfig.write_text(updated)
    print(f"Original configuration saved to {backup}")
    print("Selected 16 MB flash / 8 MB Octal PSRAM at 40 MHz, with startup memory test.")
    print("Next: ./tools/idf_phase1.sh reconfigure build")


if __name__ == "__main__":
    main()
