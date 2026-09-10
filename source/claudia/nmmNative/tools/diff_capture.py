#!/usr/bin/env python3
"""Compare an nmm101Capture directory with a native-engine capture.

The reference is the emulator's exact bit pattern.  State files are raw
big-endian 24-bit DSP words (or bytes for MCU slices); audio.nmma contains
little-endian IEEE-754 stereo samples.  The comparator reports the first few
state mismatches and both bitwise and numeric audio error, while continuing so
one run gives a useful failure inventory.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import struct
from pathlib import Path


def read_audio(path: Path) -> list[tuple[int, int, float, float]]:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"NMMA" or data[4] != 1:
        raise ValueError(f"{path}: unsupported audio.nmma header")
    count = struct.unpack_from("<I", data, 8)[0]
    expected = 12 + 8 * count
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(data)}")
    result = []
    offset = 12
    for _ in range(count):
        left_bits, right_bits = struct.unpack_from("<II", data, offset)
        left, right = struct.unpack_from("<ff", data, offset)
        result.append((left_bits, right_bits, left, right))
        offset += 8
    return result


def read_integer_audio(path: Path) -> list[tuple[int, int]]:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"NMMI" or data[4] != 1:
        raise ValueError(f"{path}: unsupported audio.int18 header")
    count = struct.unpack_from("<I", data, 8)[0]
    expected = 12 + 8 * count
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, found {len(data)}")
    return [struct.unpack_from("<ii", data, 12 + 8 * i) for i in range(count)]


def compare_audio(reference: Path, candidate: Path, limit: int) -> int:
    expected = read_audio(reference / "audio.nmma")
    actual = read_audio(candidate / "audio.nmma")
    mismatches = 0
    max_abs = 0.0
    max_index = -1
    for index, (lhs, rhs) in enumerate(zip(expected, actual)):
        for channel in range(2):
            delta = abs(lhs[2 + channel] - rhs[2 + channel])
            if math.isfinite(delta) and delta > max_abs:
                max_abs, max_index = delta, index
            if lhs[channel] != rhs[channel]:
                mismatches += 1
                if mismatches <= limit:
                    print(
                        f"audio mismatch frame={index} channel={channel} "
                        f"reference=0x{lhs[channel]:08x} candidate=0x{rhs[channel]:08x} "
                        f"delta={delta:.9g}"
                    )
    if len(expected) != len(actual):
        print(f"audio length mismatch reference={len(expected)} candidate={len(actual)}")
        mismatches += 1
    print(f"audio bit mismatches={mismatches} max_abs_error={max_abs:.9g} max_error_frame={max_index}")
    return mismatches


def compare_integer_audio(reference: Path, candidate: Path, limit: int) -> int:
    lhs_path, rhs_path = reference / "audio.int18", candidate / "audio.int18"
    expected, actual = read_integer_audio(lhs_path), read_integer_audio(rhs_path)
    mismatches = 0
    for index, (lhs, rhs) in enumerate(zip(expected, actual)):
        for channel in range(2):
            if lhs[channel] != rhs[channel]:
                mismatches += 1
                if mismatches <= limit:
                    print(f"int18 mismatch frame={index} channel={channel} reference={lhs[channel]} candidate={rhs[channel]}")
    if len(expected) != len(actual):
        print(f"int18 length mismatch reference={len(expected)} candidate={len(actual)}")
        mismatches += 1
    print(f"int18 sample mismatches={mismatches}")
    return mismatches


def compare_state(reference: Path, candidate: Path, limit: int) -> int:
    expected = {p.name: p for p in reference.glob("*.bin")}
    actual = {p.name: p for p in candidate.glob("*.bin")}
    mismatches = 0
    for name in sorted(set(expected) | set(actual)):
        if name not in expected:
            print(f"unexpected state file {name}")
            mismatches += 1
            continue
        if name not in actual:
            print(f"missing state file {name}")
            mismatches += 1
            continue
        lhs, rhs = expected[name].read_bytes(), actual[name].read_bytes()
        if lhs == rhs:
            continue
        mismatches += 1
        first = next((i for i, pair in enumerate(zip(lhs, rhs)) if pair[0] != pair[1]), min(len(lhs), len(rhs)))
        print(f"state mismatch file={name} reference_bytes={len(lhs)} candidate_bytes={len(rhs)} first_byte={first}")
        if mismatches <= limit:
            begin = max(0, first - 8)
            end = min(max(len(lhs), len(rhs)), first + 16)
            print(f"  reference[{begin}:{end}]={lhs[begin:end].hex()}")
            print(f"  candidate [{begin}:{end}]={rhs[begin:end].hex()}")
    print(f"state file mismatches={mismatches}")
    return mismatches


def compare_events(reference: Path, candidate: Path, limit: int) -> int:
    lhs_path, rhs_path = reference / "events.csv", candidate / "events.csv"
    lhs, rhs = lhs_path.read_text().splitlines(), rhs_path.read_text().splitlines()
    mismatches = 0
    for index, pair in enumerate(zip(lhs, rhs)):
        if pair[0] != pair[1]:
            mismatches += 1
            if mismatches <= limit:
                print(f"event mismatch line={index + 1}\n  reference: {pair[0]}\n  candidate:  {pair[1]}")
    if len(lhs) != len(rhs):
        print(f"event length mismatch reference={len(lhs)} candidate={len(rhs)}")
        mismatches += 1
    print(f"event line mismatches={mismatches}")
    return mismatches


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--max-mismatches", type=int, default=12)
    args = parser.parse_args()
    for directory in (args.reference, args.candidate):
        if not directory.is_dir():
            parser.error(f"capture directory does not exist: {directory}")
        # A missing artifact or a truncated capture must never become a pass.
        required = {"audio.nmma", "audio.int18", "events.csv", "manifest.csv",
                    "phases.csv", "summary.txt"}
        with (directory / "files.csv").open(newline="") as source:
            rows = list(csv.DictReader(source))
        names = [row["file"] for row in rows]
        if len(names) != len(set(names)) or not required.issubset(names):
            raise ValueError(f"{directory}: incomplete or duplicate artifact manifest")
        for row in rows:
            name = row["file"]
            if Path(name).name != name:
                raise ValueError(f"{directory}: invalid artifact filename")
            data = (directory / name).read_bytes()
            if len(data) != int(row["bytes"]) or hashlib.sha256(data).hexdigest() != row["sha256"]:
                raise ValueError(f"{directory / name}: artifact hash/size mismatch")
        if "events_full=0" not in (directory / "summary.txt").read_text().splitlines():
            raise ValueError(f"{directory}: overflow status missing or capture truncated")
    failures = compare_audio(args.reference, args.candidate, args.max_mismatches)
    failures += compare_integer_audio(args.reference, args.candidate, args.max_mismatches)
    failures += compare_state(args.reference, args.candidate, args.max_mismatches)
    failures += compare_events(args.reference, args.candidate, args.max_mismatches)
    for name in ("manifest.csv", "phases.csv", "summary.txt", "files.csv"):
        if (args.reference / name).read_bytes() != (args.candidate / name).read_bytes():
            print(f"metadata mismatch file={name}")
            failures += 1
    if failures:
        print(f"FAIL capture differences={failures}")
        return 1
    print("PASS captures are identical")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as error:
        print(f"FAIL {error}")
        raise SystemExit(1)
