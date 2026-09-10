#!/usr/bin/env python3
"""Read bounded JSON diagnostic reports from the explicitly selected Teensy.

Run with PlatformIO's Python (which supplies pyserial). This tool never flashes
or resets the board. A missing response is a failure, not a hardware test pass.
"""
import argparse
import json
import time
from pathlib import Path
import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--target", choices=["teensy36", "teensy41"], default="teensy41")
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--command", choices=["?", "g", "f", "s"], default="?")
    parser.add_argument("--require-graph-pass", action="store_true")
    args = parser.parse_args()
    if not 0 < args.seconds <= 60:
        parser.error("--seconds must be in (0, 60]")
    candidates = [p.device for p in list_ports.comports() if p.vid == 0x16C0]
    if not args.port and len(candidates) != 1:
        parser.error(f"expected one Teensy serial port, found {candidates}; use --port")
    records = []
    with serial.Serial(args.port or candidates[0], 115200, timeout=0.2) as connection:
        connection.write((args.command + "\n").encode("ascii"))
        deadline = time.monotonic() + args.seconds
        while time.monotonic() < deadline:
            line = connection.readline(8192)
            if not line:
                continue
            try:
                record = json.loads(line)
            except (ValueError, UnicodeError):
                print(f"Non-JSON board output: {line!r}")
                continue
            if record.get("target") != args.target:
                raise ValueError("unexpected diagnostic target")
            records.append(record)
            print(json.dumps(record, sort_keys=True), flush=True)
    if not records:
        raise RuntimeError("No Teensy diagnostic reports received")
    if args.output:
        args.output.write_text(json.dumps(records, indent=2) + "\n")
    if args.require_graph_pass:
        result = records[-1]
        if (result.get("stage") != "native-101" or
                result.get("offline_blocks") != 912 or
                result.get("offline_passed") != 912 or
                result.get("offline_failed") != 0 or
                not result.get("module_graph_verified")):
            raise RuntimeError("Board did not pass the complete 101 module graph oracle")


if __name__ == "__main__":
    main()
