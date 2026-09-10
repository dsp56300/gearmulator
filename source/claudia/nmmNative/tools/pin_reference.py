#!/usr/bin/env python3
"""Save local reference provenance, including dirty submodule patches."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
NATIVE = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--output", type=Path, default=NATIVE / "build/provenance")
    parser.add_argument("--emulator-build", type=Path, default=ROOT / "build/nmm")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    expected = "0ccbffa696f4aa65baa53768695dc02b3afa61ce572006b532cf278515759c3c"
    if digest(args.firmware) != expected:
        raise ValueError("Unexpected OS 3.03b firmware hash")
    record = {"firmware_sha256": expected, "repositories": {}, "files": {}}
    for name, repo in [("root", ROOT), ("dsp56300", ROOT / "source/cpu/dsp56300"),
                       ("mc68k", ROOT / "source/cpu/mc68k")]:
        patch = run("git", "diff", "HEAD", "--binary", cwd=repo)
        (args.output / (name + ".patch")).write_bytes(patch)
        record["repositories"][name] = {
            "head": run("git", "rev-parse", "HEAD", cwd=repo).decode().strip(),
            "diff_sha256": hashlib.sha256(patch).hexdigest(),
            "status": run("git", "status", "--short", cwd=repo).decode().splitlines(),
        }
    files = list((ROOT / "source/claudia/nmm/nmmLib").glob("*"))
    files += [ROOT / "nord-micro-modular/patches/101.pch",
              ROOT / "nord-micro-modular/analysis/architecture/101.generated.asm"]
    files += list(args.emulator_build.rglob("*.a"))
    files += [args.emulator_build / "CMakeCache.txt"]
    for file in files:
        if file.is_file():
            record["files"][str(file.resolve().relative_to(ROOT))] = digest(file)
    for tool, argv in [("disassembler", ["dsp56300-disasm", "--version"]),
                       ("compiler", ["c++", "--version"]), ("cmake", ["cmake", "--version"])]:
        record[tool] = run(*argv).decode().splitlines()[0]
    (args.output / "manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print(args.output / "manifest.json")


if __name__ == "__main__":
    main()
