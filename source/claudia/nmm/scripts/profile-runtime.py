#!/usr/bin/env python3
"""Sample an isolated paced NMM run on macOS; retain raw stacks and test output.

Profiler-attached CPU/callback times are diagnostic, not performance baselines.
Use stress-runtime.py without the sampler for throughput measurements.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--patch', type=Path, default=Path('nord-micro-modular/patches/FourVoices.pch'))
    parser.add_argument('--binary', type=Path, default=Path('build/nmm-ui/source/claudia/nmm/nmmJucePlugin/nmmRealtimeStressTests'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('This capture uses macOS sample; use your platform profiler on nmmRealtimeStressTests elsewhere')
    for path in (args.binary, args.firmware, args.patch):
        if not path.is_file():
            parser.error(f'Missing file: {path}')
    args.output.mkdir(parents=True, exist_ok=False)
    with (args.output / 'playback.log').open('w') as log:
        process = subprocess.Popen([str(args.binary.resolve()), str(args.firmware), str(args.patch), '30', '2', '128'],
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            time.sleep(5)  # Exclude ROM boot and initial JIT compilation from the sample.
            capture = subprocess.run(['/usr/bin/sample', str(process.pid), '10', '1', '-file',
                                      str(args.output / 'stacks.sample')], capture_output=True, text=True, timeout=30)
            (args.output / 'sampler.log').write_text(capture.stdout + capture.stderr)
            code = process.wait(timeout=45)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    result = {'platform': platform.platform(), 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
              'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'sample_exit_code': capture.returncode, 'playback_exit_code': code,
              'note': 'Sampling perturbs scheduling; use an unprofiled run for CPU and latency comparisons.'}
    (args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return int(capture.returncode != 0 or code != 0)


if __name__ == '__main__':
    raise SystemExit(main())
