#!/usr/bin/env python3
"""Compare per-frame graph probe DAC words with an NMMI audio capture."""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path


def read_audio(path: Path) -> list[tuple[int, int]]:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"NMMI" or data[4:8] != struct.pack("<I", 1):
        raise ValueError(f"{path}: expected NMMI-v1 audio")
    count = struct.unpack_from("<I", data, 8)[0]
    words = struct.unpack_from(f"<{count * 2}i", data, 12)
    if len(data) != 12 + count * 8:
        raise ValueError(f"{path}: length does not match header")
    return list(zip(words[::2], words[1::2]))


def read_graph(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        rows = [row for row in csv.DictReader(line for line in stream if not line.startswith("#"))]
    required = {"host_frame", "phase", "dsp_frame_after", "dsp_cycles", "x04", "x05",
                "y6c0", "y6e0", "left_int18", "right_int18"}
    if not rows or not required.issubset(rows[0]):
        raise ValueError(f"{path}: graph capture header is incomplete")
    return rows


def read_dma_boundaries(path: Path) -> list[tuple[int, tuple[int, int]]]:
    """Return the final observed boundary for each DSP audio frame.

    Hardware::writeDmaBoundaryTrace emits two observations around each
    peripheral poll. The final row for a frame is the post-poll state from
    which the codec-facing mix selector can be checked.
    """
    lines = path.read_text().splitlines()
    if not lines or not lines[0].startswith("# boundary observations"):
        raise ValueError(f"{path}: expected Hardware DMA boundary trace")
    if "truncated=1" in lines[0]:
        raise ValueError(f"{path}: boundary trace is truncated")
    rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
    required = {"frame", "mix_selector", "y6c0", "y6c1", "y6e0", "y6e1"}
    if not rows or not required.issubset(rows[0]):
        raise ValueError(f"{path}: DMA boundary header is incomplete")
    final: dict[int, dict[str, str]] = {}
    for row in rows:
        final[int(row["frame"])] = row
    result = []
    for frame in sorted(final):
        row = final[frame]
        selector = int(row["mix_selector"])
        if selector == 0x6C0:
            selected = (int(row["y6c0"]), int(row["y6c1"]))
        elif selector == 0x6E0:
            selected = (int(row["y6e0"]), int(row["y6e1"]))
        else:
            raise ValueError(f"unexpected DMA mix selector 0x{selector:x} at frame {frame}")
        result.append((frame, tuple(signed18_word(word) for word in selected)))
    return result


def signed18_word(value: int) -> int:
    value &= 0x3FFFF
    return (value ^ 0x20000) - 0x20000


def check_dma_mapping(rows: list[dict[str, str]], boundaries: list[tuple[int, tuple[int, int]]]) -> None:
    if len(boundaries) < len(rows):
        raise ValueError(f"DMA trace has {len(boundaries)} frames for {len(rows)} graph rows")
    for index, row in enumerate(rows):
        frame, expected = boundaries[index]
        actual = (int(row["left_int18"]), int(row["right_int18"]))
        if actual != expected:
            raise ValueError(f"DMA/DAC mismatch at host_frame={row['host_frame']} "
                             f"produced_frame={frame}: expected={expected} actual={actual}")
    print(f"PASS DMA/DAC mapping: checked={len(rows)} "
          f"produced_frames={boundaries[0][0]}..{boundaries[len(rows)-1][0]}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("graph_csv", type=Path)
    parser.add_argument("audio_int18", type=Path)
    parser.add_argument("--dma-csv", type=Path,
                        help="optional Hardware::writeDmaBoundaryTrace CSV to validate selector/DAC mapping")
    parser.add_argument("--offset", type=int, default=0,
                        help="audio frame offset corresponding to graph row zero")
    args = parser.parse_args()
    if args.offset < 0:
        parser.error("--offset must be non-negative")

    rows = read_graph(args.graph_csv)
    if args.dma_csv is not None:
        check_dma_mapping(rows, read_dma_boundaries(args.dma_csv))
    audio = read_audio(args.audio_int18)
    compared = min(len(rows), len(audio) - args.offset)
    if compared <= 0:
        raise ValueError("no overlapping frames")

    mismatches = []
    for index in range(compared):
        row = rows[index]
        actual = (int(row["left_int18"]), int(row["right_int18"]))
        expected = audio[args.offset + index]
        if actual != expected:
            mismatches.append((index, row, actual, expected))
            if len(mismatches) == 8:
                break

    if mismatches:
        for index, row, actual, expected in mismatches:
            print(f"mismatch row={index} host={row['host_frame']} phase={row['phase']} "
                  f"dsp_frame_after={row['dsp_frame_after']} expected={expected} actual={actual} "
                  f"x04={row['x04']} x05={row['x05']} y6c0={row['y6c0']} y6e0={row['y6e0']}")
        return 1

    print(f"PASS graph DAC mapping: compared={compared} offset={args.offset} "
          f"graph_rows={len(rows)} audio_frames={len(audio)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
