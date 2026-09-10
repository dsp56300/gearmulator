#!/usr/bin/env python3
"""Extract the fixed 101 MCU/DSP dependency report.

This is deliberately a small, deterministic evidence tool rather than a
patch compiler. It does not infer parameter conversion or velocity formulas.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


FIRMWARE_SHA256 = "0ccbffa696f4aa65baa53768695dc02b3afa61ce572006b532cf278515759c3c"
PATCH_SHA256 = "1960af3ab94ec9317284eb5b877f1dc133e2094da72238e86424a98487ffbef8"
FIRMWARE_BASE = 0x100000


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def patch_records(path: Path) -> tuple[list[dict], list[list[int]]]:
    section = None
    section_area = None
    module_rows = []
    parameter_rows = []
    cable_rows = []
    for raw in path.read_bytes().splitlines():
        text = raw.decode("ascii").strip()
        if text.startswith("["):
            section = None if text.startswith("[/") else text[1:-1]
            section_area = None
            continue
        tokens = text.split()
        try:
            fields = [int(value) for value in tokens]
        except ValueError:
            # Section labels such as [Header] and [Modules] are part of the
            # patch container, not records.
            continue
        if section in {"ModuleDump", "ParameterDump", "CableDump"}:
            if section_area is None:
                section_area, *fields = fields
                if not fields:
                    continue
            if section == "ModuleDump" and len(fields) == 4:
                module_rows.append([section_area, *fields])
            elif section == "ParameterDump" and len(fields) >= 3:
                parameter_rows.append([section_area, *fields])
            elif section == "CableDump" and len(fields) == 7:
                cable_rows.append([section_area, *fields])
    modules = []
    for area, slot, module_type, x, y in module_rows:
        modules.append({
            "area": area,
            "slot": slot,
            "type": module_type,
            "x": x,
            "y": y,
            "parameters": [],
        })
    for row in parameter_rows:
        area, slot, module_type, count = row[:4]
        for module in modules:
            if module["area"] == area and module["slot"] == slot and module["type"] == module_type:
                module["parameters"] = row[4 : 4 + count]
                break
    return modules, cable_rows


def words_at(firmware: bytes, address: int, count: int) -> list[int]:
    offset = address - FIRMWARE_BASE
    raw = firmware[offset : offset + count * 4]
    if len(raw) != count * 4:
        raise ValueError(f"firmware does not contain {count} words at {address:#x}")
    return [int.from_bytes(raw[index : index + 4], "big") for index in range(0, len(raw), 4)]


def disasm_version() -> str | None:
    executable = shutil.which("dsp56300-disasm")
    if executable is None:
        return None
    result = subprocess.run([executable, "--version"], check=False, capture_output=True, text=True)
    return (result.stdout or result.stderr).strip() or executable


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("patch", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    firmware = args.firmware.read_bytes()
    patch = args.patch.read_bytes()
    modules, cables = patch_records(args.patch)
    table_voices = words_at(firmware, 0x14acb2, 128)
    host_ports = words_at(firmware, 0x154468, 8)
    master = words_at(firmware, 0x14c3b2, 128)

    report = {
        "firmware": {
            "sha256": sha256(firmware),
            "expected_sha256": FIRMWARE_SHA256,
            "base": hex(FIRMWARE_BASE),
            "size": len(firmware),
        },
        "patch": {
            "sha256": sha256(patch),
            "expected_sha256": PATCH_SHA256,
            "modules": modules,
            "cables": cables,
        },
        "module_banks": {
            "7": {"sample": [12, 5, 240], "control": [0, 0, 0]},
            "92": {"sample": [6, 4, 34], "control": [4, 6, 39]},
            "20": {"sample": [0, 0, 6], "control": [15, 3, 32]},
            "4": {"sample": [1, 1, 12], "control": [0, 0, 0]},
        },
        "voice_transport": {
            "pitch_mcu_sequence": "((note << 23) - 0x20000000)",
            "pitch_dsp_word": "mask24((note << 15) - 0x200000)",
            "gate_on": "0x200000",
            "gate_off": "0x000000",
            "x0d_table": "0x14acb2 + velocity * 4",
            "velocity_formula": "(velocity * 0x200000 + 63) // 127",
            "observed_velocity_100_x0d": "0x193265",
        },
        "smoothing": {
            "body": ["0x10c958", "0x10c99a"],
            "record_offsets": {"active": "0x204", "area": "0x205", "shift": "0x207", "target": "0x208", "current_pointer": "0x20c"},
            "default_shift_observed": 6,
            "recurrence": "current -= arithmetic_shift_right(current - target, shift)",
            "snap_condition": "-255 < current - target < 255",
        },
        "dsp_graph_setup": {
            "sample_entry": "0x1ba",
            "control_entry": "0x175",
            "control_endpoint": "0x1b9",
            "sample_graph_r3_r4_after_prefix": "0x60",
            "n2": "0x7bc",
            "n5": "0x780",
            "r6": "0x620",
            "shared_x_window": "0x780..0x7ff",
            "shared_y_window": "0x780..0x7ff",
        },
        "tables": {
            "0x154468": {"role": "host-port pointer table", "count": 8, "words": [f"0x{value:08x}" for value in host_ports]},
            "0x14acb2": {"role": "velocity conversion table for X:0x0d", "count": 128, "words": [f"0x{value:08x}" for value in table_voices], "sha256": sha256(firmware[0x14acb2 - FIRMWARE_BASE : 0x14acb2 - FIRMWARE_BASE + 128 * 4])},
            "0x14c3b2": {"role": "master coefficient table", "count": 128, "value_for_101_output_115": f"0x{master[115]:08x}", "sha256": sha256(firmware[0x14c3b2 - FIRMWARE_BASE : 0x14c3b2 - FIRMWARE_BASE + 128 * 4])},
        },
        "dsp56300_disasm": disasm_version(),
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
