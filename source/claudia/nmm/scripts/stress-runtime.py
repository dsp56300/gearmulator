#!/usr/bin/env python3
"""Run paced tests sequentially, saving full logs and a machine-readable result.

Use an otherwise idle machine. This tests the shared host framework, not a DAW.
Both phases default to five minutes. Firmware is supplied by the caller.
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
    parser.add_argument('--patches', type=Path, default=Path('nord-micro-modular/patches'))
    parser.add_argument('--build', type=Path, default=Path('build/nmm-ui'))
    parser.add_argument('--seconds', type=int, default=300)
    parser.add_argument('--instances', type=int, default=2)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--callback-cpu', action='store_true', help='Opt-in callback/worker timing diagnostics')
    args = parser.parse_args()
    if not 40 <= args.seconds <= 3600 or not 1 <= args.instances <= 8:
        parser.error('Use 40..3600 seconds and 1..8 instances')
    if not args.firmware.is_file():
        parser.error('Firmware file does not exist')
    root = args.build / 'source/claudia/nmm/nmmJucePlugin'
    cases = [
        ('host', root / 'nmmHostStressTests', [str(args.firmware), str(args.patches), str(args.seconds), str(args.instances), '--concurrent-state'] + (['--callback-cpu'] if args.callback_cpu else [])),
        ('steady', root / 'nmmRealtimeStressTests', [str(args.firmware), str(args.patches / 'FourVoices.pch'), str(args.seconds), str(args.instances), '128']),
    ]
    for _, binary, _ in cases:
        if not binary.is_file():
            parser.error(f'Build {binary.name} first')
    args.output.mkdir(parents=True, exist_ok=False)
    result = {'platform': platform.platform(), 'machine': platform.machine(),
              'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'seconds_per_phase': args.seconds, 'instances': args.instances, 'concurrent_state': True, 'callback_cpu': args.callback_cpu, 'cases': []}
    for name, binary, arguments in cases:
        log_path = args.output / (name + '.log')
        start = time.monotonic()
        with log_path.open('w') as log:
            # A timeout fails the suite instead of leaving an unattended hung host.
            try:
                process = subprocess.run([str(binary.resolve()), *arguments], stdout=log,
                                         stderr=subprocess.STDOUT, timeout=args.seconds * 3 + 120)
                code = process.returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
        lines = log_path.read_text(errors='replace').splitlines()
        summary = [line for line in lines if line.startswith(('wall_seconds=', 'seconds=', 'instance=', 'format_rate=', 'worker_job_max_us=', 'callback_tail ', 'queue_failure ', 'autosave_failure ', 'PASS '))]
        passed = code == 0 and any(line.startswith('PASS ') for line in summary)
        result['cases'].append({'name': name, 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                                'exit_code': code, 'passed': passed,
                                'wall_seconds': time.monotonic() - start, 'summary': summary})
        (args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        print(name, 'PASS' if passed else 'FAIL', *summary, sep='\n', flush=True)
        if not passed:
            print('See', log_path, flush=True)
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
