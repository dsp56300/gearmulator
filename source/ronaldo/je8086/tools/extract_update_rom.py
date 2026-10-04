#!/usr/bin/env python3
"""Reconstruct a JP-8000 ROM image from Roland update SMF files.

The updater stores firmware in Roland DT1 SysEx packets.  Packet addresses are
four base-128 digits.  Firmware bytes are packed in groups containing one
high-bit mask followed by as many as seven low-bit bytes.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys


def read_vlq(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for _ in range(4):
        if offset >= len(data):
            raise ValueError("truncated MIDI variable-length quantity")
        byte = data[offset]
        offset += 1
        value = (value << 7) | (byte & 0x7F)
        if byte < 0x80:
            return value, offset
    raise ValueError("invalid MIDI variable-length quantity")


def midi_sysex_events(path: Path) -> list[bytes]:
    data = path.read_bytes()
    if data[:4] != b"MThd" or len(data) < 14:
        raise ValueError(f"{path}: not a Standard MIDI File")

    header_size = struct.unpack_from(">I", data, 4)[0]
    offset = 8 + header_size
    events: list[bytes] = []

    while offset + 8 <= len(data):
        chunk_type = data[offset : offset + 4]
        chunk_size = struct.unpack_from(">I", data, offset + 4)[0]
        offset += 8
        chunk = data[offset : offset + chunk_size]
        offset += chunk_size
        if chunk_type != b"MTrk":
            continue

        pos = 0
        running_status: int | None = None
        while pos < len(chunk):
            _, pos = read_vlq(chunk, pos)
            if pos >= len(chunk):
                break
            status = chunk[pos]
            if status < 0x80:
                if running_status is None:
                    raise ValueError(f"{path}: invalid running status")
                status = running_status
            else:
                pos += 1

            if status in (0xF0, 0xF7):
                running_status = None
                size, pos = read_vlq(chunk, pos)
                payload = chunk[pos : pos + size]
                if len(payload) != size:
                    raise ValueError(f"{path}: truncated SysEx event")
                pos += size
                events.append(payload)
            elif status == 0xFF:
                running_status = None
                if pos >= len(chunk):
                    raise ValueError(f"{path}: truncated meta event")
                pos += 1  # meta type
                size, pos = read_vlq(chunk, pos)
                pos += size
            else:
                running_status = status
                event_type = status & 0xF0
                size = 1 if event_type in (0xC0, 0xD0) else 2
                pos += size

            if pos > len(chunk):
                raise ValueError(f"{path}: truncated MIDI event")

    return events


def base128_address(digits: bytes) -> int:
    value = 0
    for digit in digits:
        if digit >= 0x80:
            raise ValueError("non-7-bit Roland address digit")
        value = (value << 7) | digit
    return value


def unpack_firmware(encoded: bytes) -> bytes:
    result = bytearray()
    offset = 0
    while offset < len(encoded):
        group_size = min(8, len(encoded) - offset)
        if group_size < 2:
            raise ValueError("packed firmware ends with a high-bit mask only")
        high_bits = encoded[offset]
        low = encoded[offset + 1 : offset + group_size]
        result.extend(byte | (((high_bits >> index) & 1) << 7) for index, byte in enumerate(low))
        offset += group_size
    return bytes(result)


def extract_packets(path: Path) -> list[tuple[int, bytes]]:
    packets: list[tuple[int, bytes]] = []
    for payload in midi_sysex_events(path):
        if payload.endswith(b"\xF7"):
            payload = payload[:-1]
        # Roland, device 0, JP-8000 model 00 06, DT1 command.
        if len(payload) < 11 or payload[:5] != b"\x41\x00\x00\x06\x12":
            continue
        address_digits = payload[5:9]
        encoded = payload[9:-1]
        checksum = payload[-1]
        expected = (-sum(address_digits + encoded)) & 0x7F
        if checksum != expected:
            raise ValueError(
                f"{path}: bad Roland checksum at {base128_address(address_digits):#x}: "
                f"got {checksum:#04x}, expected {expected:#04x}"
            )
        packets.append((base128_address(address_digits), unpack_firmware(encoded)))
    if not packets:
        raise ValueError(f"{path}: no JP-8000 DT1 firmware packets found")
    return packets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="directory containing _00001.mid ... _00008.mid")
    parser.add_argument("output", type=Path, help="output ROM image")
    args = parser.parse_args()

    paths = sorted(args.input.glob("_0000[1-8].mid"))
    if len(paths) != 8:
        parser.error(f"expected eight update files, found {len(paths)} in {args.input}")

    regions: dict[int, bytes] = {}
    packet_count = 0
    for path in paths:
        for address, payload in extract_packets(path):
            packet_count += 1
            for index, byte in enumerate(payload):
                absolute = address + index
                previous = regions.get(absolute)
                if previous is not None and previous != bytes([byte]):
                    raise ValueError(f"conflicting data at ROM offset {absolute:#x}")
                regions[absolute] = bytes([byte])

    first = min(regions)
    last = max(regions) + 1
    missing = [address for address in range(first, last) if address not in regions]
    if first != 0 or missing:
        sample = ", ".join(f"{address:#x}" for address in missing[:8])
        raise ValueError(f"firmware image is not contiguous from zero; missing: {sample}")

    image = b"".join(regions[address] for address in range(last))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    digest = hashlib.sha256(image).hexdigest()
    print(f"wrote {len(image)} bytes from {packet_count} DT1 packets to {args.output}")
    print(f"sha256 {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
