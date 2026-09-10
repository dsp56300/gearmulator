#!/usr/bin/env python3
"""Negative tests for the capture acceptance gate, without requiring firmware."""
import csv
import hashlib
import importlib.util
import io
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).with_name("diff_capture.py")


def fixture(directory):
    directory.mkdir()
    (directory / "audio.nmma").write_bytes(b"NMMA" + struct.pack("<IIff", 1, 1, 0.0, 0.0))
    (directory / "audio.int18").write_bytes(b"NMMI" + struct.pack("<IIii", 1, 1, 0, 0))
    (directory / "events.csv").write_text("event\n")
    (directory / "manifest.csv").write_text("key,value\ncapture_version,1\n")
    (directory / "phases.csv").write_text("phase,start_frame,frame_count\nnote,0,1\n")
    (directory / "summary.txt").write_text("events_full=0\n")
    (directory / "state.bin").write_bytes(bytes(3))
    reindex(directory)


def reindex(directory):
    with (directory / "files.csv").open("w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(["file", "bytes", "sha256"])
        for path in sorted(directory.iterdir()):
            if path.name != "files.csv":
                data = path.read_bytes()
                writer.writerow([path.name, len(data), hashlib.sha256(data).hexdigest()])


class ComparatorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.a, self.b = (Path(self.temp.name) / name for name in ("a", "b"))
        fixture(self.a)
        fixture(self.b)

    def result(self):
        return subprocess.run([sys.executable, str(TOOL), str(self.a), str(self.b)],
                              capture_output=True, text=True)

    def test_identical(self):
        self.assertEqual(self.result().returncode, 0)

    def test_missing_integer_oracle(self):
        (self.b / "audio.int18").unlink()
        reindex(self.b)
        self.assertNotEqual(self.result().returncode, 0)

    def test_corrupted_artifact(self):
        (self.b / "state.bin").write_bytes(b"\0\0\1")
        self.assertNotEqual(self.result().returncode, 0)

    def test_valid_but_different_samples(self):
        (self.b / "audio.int18").write_bytes(b"NMMI" + struct.pack("<IIii", 1, 1, 1, 0))
        reindex(self.b)
        self.assertNotEqual(self.result().returncode, 0)

    def test_overflow(self):
        (self.b / "summary.txt").write_text("events_full=1\n")
        reindex(self.b)
        self.assertNotEqual(self.result().returncode, 0)


if __name__ == "__main__":
    unittest.main()
