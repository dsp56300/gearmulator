#!/usr/bin/env python3
"""Sequential opt-in block-limit correctness and paced CPU experiment."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    parser.add_argument("patches", type=Path)
    parser.add_argument("--build", type=Path, default=Path("build/nmm-ui"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--deadline-graphs", "--sample-regions", dest="sample_regions", action="store_true",
                        help="Compare baseline against deadline-checked graph continuations; all blocks stay at 16")
    parser.add_argument("--host-stress-seconds", type=int, default=0,
                        help="Optional longer host test per policy (40..3600 seconds)")
    parser.add_argument("--instances", type=int, default=2)
    parser.add_argument("--offline-host-stress", action="store_true",
                        help="Run host transitions without realtime pacing; keep CPU profiling paced")
    args = parser.parse_args()
    policy = []
    if not 10 <= args.seconds <= 300:
        parser.error("seconds must be 10..300")
    if args.host_stress_seconds and not 40 <= args.host_stress_seconds <= 3600:
        parser.error("host-stress-seconds must be zero or 40..3600")
    if not 1 <= args.instances <= 8:
        parser.error("instances must be 1..8")
    args.output.mkdir(parents=True, exist_ok=True)
    consoles = args.build / "source/claudia/nmm/nmmTestConsole"
    plugins = args.build / "source/claudia/nmm/nmmJucePlugin"
    results = []

    def run(name, command, kind, limit):
        started = time.monotonic()
        with (args.output / (name + ".log")).open("w") as log:
            try:
                code = subprocess.run([str(x) for x in command], stdout=log,
                                      stderr=subprocess.STDOUT,
                                      timeout=(args.host_stress_seconds * 2 + 180 if kind == "host-stress" else args.seconds + 180)).returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        result = dict(name=name, kind=kind, block_limit=limit, exit=code,
                      wall_seconds=time.monotonic() - started, deadline_graphs=bool(policy), effective_block_limit=16 if policy else limit)
        text = (args.output / (name + ".log")).read_text()
        result["summary"] = [line for line in text.splitlines()
                             if line.startswith(("MEASURE", "PASS", "split=", "playback_cpu_seconds="))]
        if code != 0:
            result["failure_tail"] = text.splitlines()[-5:]
        match = re.search(r"MEASURE (.*)", text)
        if match:
            result["measurement"] = {k: float(v) for k, v in
                                     (part.split("=") for part in match[1].split())}
        results.append(result)
        (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(json.dumps(result), flush=True)
        return code

    for limit in ((16, 64) if args.sample_regions else (16, 32, 64)):
        policy = ["--deadline-graphs"] if args.sample_regions and limit>16 else []
        runtime_exit = run(f"runtime-{limit}", [consoles / "nmmRuntimeTests", args.firmware,
                                "64", "cooperative", limit] + policy, "runtime", limit)
        if runtime_exit != 0:
            run(f"runtime-linked-{limit}", [consoles / "nmmRuntimeTests", args.firmware,
                                            "64", "cooperative", limit, "--linked-only"] + policy,
                "runtime-linked", limit)
        matrix = [consoles / "nmmPatchRegressionTests", args.firmware, args.patches,
                  "--audio-driven", "64", "--cooperative", "--linked-jit",
                  "--block-limit", limit] + policy
        run(f"matrix-{limit}", matrix, "matrix", limit)
        run(f"diagnostics-{limit}", matrix + ["--diagnostics"], "diagnostics", limit)
        run(f"chords-{limit}", [plugins / "nmmChordTests", args.firmware,
                               args.patches / "FourVoices.pch", "--realtime",
                               "--block-limit", limit] + policy, "chords", limit)

        if args.host_stress_seconds:
            run(f"host-{limit}", [plugins / "nmmHostStressTests", args.firmware,
                                  args.patches, args.host_stress_seconds, args.instances,
                                  "--concurrent-state", "--block-limit", limit] + policy + (["--offline"] if args.offline_host_stress else []),
                "host-stress", limit)

    # Reverse the order for the second pass to expose temperature/scheduling drift.
    # No tracing, diagnostics, or concurrent subprocesses during CPU measurement.
    for index, limit in enumerate((16, 64, 64, 16) if args.sample_regions else (16, 32, 64, 64, 32, 16)):
        policy = ["--deadline-graphs"] if args.sample_regions and limit>16 else []
        name = f"profile-{index}-{limit}"
        run(name, [consoles / "nmmJitProfile", args.firmware,
                   args.patches / "FourVoices.pch", args.output / name,
                   args.seconds, "--block-limit", limit] + policy, "profile", limit)
    return int(any(result["exit"] != 0 for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
