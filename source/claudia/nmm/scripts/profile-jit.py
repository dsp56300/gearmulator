#!/usr/bin/env python3
"""macOS native JIT block attribution using stable before/after maps.

Maps are captured without attaching a DSP debugger. Shared native functions
retain all DSP-PC aliases. Unmapped generated helpers are reported separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import time


def attribute(map_text, sample_text):
    ranges = {}
    for line in map_text.splitlines():
        values = [int(v, 16) for v in line.split()]
        start, size, pc, words, mode, *opcodes = values
        if len(opcodes) != words or not size:
            raise ValueError('Invalid JIT map record')
        ranges.setdefault((start, size), []).append({'dsp_pc': hex(pc), 'words': words, 'mode': hex(mode), 'opcodes': opcodes})
    section = sample_text.split('Sort by top of stack, same collapsed (when >= 5):', 1)[1].split('Binary Images:', 1)[0]
    hits = {}
    unmapped = []
    for line in section.splitlines():
        match = re.search(r'\?\?\?.*\[0x([0-9a-fA-F]+)\]\s+(\d+)\s*$', line)
        if not match:
            continue
        address, count = int(match[1], 16), int(match[2])
        matches = [key for key in ranges if key[0] <= address < key[0] + key[1]]
        if len(matches) == 1:
            key = matches[0]
            hits[key] = hits.get(key, 0) + count
        else:
            unmapped.append({'address': hex(address), 'leaf_observations': count, 'matching_ranges': len(matches)})
    blocks = [{'native_start': hex(start), 'native_size': size, 'leaf_observations': count,
               'aliases': ranges[(start, size)]} for (start, size), count in sorted(hits.items(), key=lambda item: -item[1])]
    return {'mapped_leaf_observations': sum(hits.values()), 'unmapped_leaf_observations': sum(r['leaf_observations'] for r in unmapped),
            'blocks': blocks, 'unmapped': unmapped,
            'note': 'Counts come from sample collapsed leaf rows >=5, not inclusive CPU percentages. Helper/trampoline addresses may remain unmapped.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--patch', type=Path, default=Path('nord-micro-modular/patches/FourVoices.pch'))
    parser.add_argument('--binary', type=Path, default=Path('build/nmm-ui/source/claudia/nmm/nmmTestConsole/nmmJitProfile'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Capture requires macOS sample')
    args.output.mkdir(parents=True, exist_ok=False)
    prefix = args.output.resolve() / 'jit'
    log_path = args.output / 'playback.log'
    with log_path.open('w') as log:
        process = subprocess.Popen([str(args.binary.resolve()), str(args.firmware), str(args.patch), str(prefix), '20'], stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while 'READY\n' not in log_path.read_text(errors='replace'):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Profile startup failed; see playback.log')
                time.sleep(.1)
            capture = subprocess.run(['/usr/bin/sample', str(process.pid), '10', '1', '-file', str(args.output / 'stacks.sample')], capture_output=True, text=True, timeout=30)
            (args.output / 'sampler.log').write_text(capture.stdout + capture.stderr)
            if capture.returncode or process.wait(timeout=30):
                raise RuntimeError('Sampling or map stability check failed')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    # Native bytes are captured without a debugger. Disassemble an object only;
    # copied absolute addresses are not relocatable executable code.
    native_object = args.output / 'jit.native.o'
    subprocess.run(['xcrun', 'clang', '-arch', 'arm64' if platform.machine() == 'arm64' else 'x86_64',
                    '-c', str(prefix) + '.native.s', '-o', str(native_object)], check=True, timeout=30)
    with (args.output / 'jit.native.asm').open('w') as assembly:
        subprocess.run(['xcrun', 'llvm-objdump', '--disassemble', str(native_object)], stdout=assembly, check=True, timeout=30)
    before = Path(str(prefix) + '.before.map').read_text()
    if before != Path(str(prefix) + '.after.map').read_text():
        raise RuntimeError('Changed JIT map; refusing attribution')
    result = attribute(before, (args.output / 'stacks.sample').read_text())
    result['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    result['firmware_sha256'] = hashlib.sha256(args.firmware.read_bytes()).hexdigest()
    (args.output / 'attribution.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Mapped', result['mapped_leaf_observations'], 'unmapped', result['unmapped_leaf_observations'])
    for block in result['blocks'][:12]:
        print(block['leaf_observations'], ','.join(a['dsp_pc'] for a in block['aliases']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
