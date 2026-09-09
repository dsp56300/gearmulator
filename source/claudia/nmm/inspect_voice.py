#!/usr/bin/env python3
"""Offline snapshot inspection; optional OS 3.03b / 101.pch voice regression.

No emulator work runs here. Snapshots are taken between render calls. Program
addresses and validation expectations are specific to this compiled fixture.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess


def read_state(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        area, address, value = line.split()
        key = address if area == 'reg' else int(address, 16)
        result[area, key] = int(value, 16)
    return result


def describe(state):
    slot = 0x175f40
    cpu = lambda offset: state['cpu', slot + offset]
    areas = []
    for offset in (0x467a, 0x4c66):
        count = cpu(offset + 0x1fc)
        if count > 32:
            raise ValueError('Invalid allocated voice count')
        areas.append({
            'name': 'common' if offset == 0x467a else 'polyphonic',
            'allocated': count, 'used': cpu(offset + 0x224),
            'bindings': [bytes(cpu(offset + 0x228 + 14*i + j) for j in range(14)).hex()
                         for i in range(count)],
            'voices': [{'index': cpu(offset + 0x3e8 + 14*i + 8),
                        'note': cpu(offset + 0x3e8 + 14*i + 9),
                        'velocity': cpu(offset + 0x3e8 + 14*i + 10),
                        'state': cpu(offset + 0x3e8 + 14*i + 12)}
                       for i in range(count)],
        })
    return {'cpu_pc': hex(state['reg', 'cpu_pc']),
            'dsp_pc': hex(state['reg', 'pc']), 'la': hex(state['reg', 'la']),
            'sample_entry': hex(state['P', 0x17]),
            'highest_note': cpu(0x3490), 'requested_voices': cpu(0x5a1a), 'areas': areas}


def verify_101(states, wav):
    def require(condition, message):
        if not condition:
            raise ValueError(message)
    ready, held, released = (states[p] for p in ('ready', 'held', 'released'))
    require(ready['reg', 'cpu_pc'] == 0x100b38, 'MIDI readiness precedes application loop')
    require(held['P', 0x17] == 0x1ba and held['reg', 'la'] + 1 == 0x1ba
            and (held.get(('loopend', 0x1ba)) == 1 or held.get(('loop', 0x16a)) == 0x1ba),
            '101 control/sample layout or JIT loop metadata mismatch')
    require([s['X', 0xe] for s in (ready, held, released)] == [0, 0x200000, 0], 'Gate regression')
    require([s['X', 0x13] for s in (ready, held, released)] == [0, 0x1fffff, 0], 'Envelope regression')
    require(held['X', 0xf] == 0xfe0000 and held['X', 0xd] == 0x193265, 'Note 60 / velocity 100 regression')
    require([s['cpu', 0x1793d0] for s in (ready, held, released)] == [0, 60, 0], 'CPU note lifetime regression')
    for base in (0x175f40 + 0x467a, 0x175f40 + 0x4c66):
        require(all(s['cpu', base + 0x1fc] == 1 for s in (ready, held, released)), 'Voice allocation regression')
        require([s['cpu', base + 0x224] for s in (ready, held, released)] == [0, 1, 0], 'Used voice count regression')
        require(held['cpu', base + 0x3e8 + 9] == 60 and held['cpu', base + 0x3e8 + 10] == 100,
                'CPU voice record note/velocity regression')
    raw = Path(wav).read_bytes()
    require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'Not a WAV')
    chunks, offset = {}, 12
    while offset + 8 <= len(raw):
        name, size = struct.unpack_from('<4sI', raw, offset)
        chunks[name] = raw[offset+8:offset+8+size]
        offset += 8 + size + (size & 1)
    fmt, channels, rate, _, _, bits = struct.unpack_from('<HHIIHH', chunks[b'fmt '])
    require((fmt, channels, rate, bits) == (3, 2, 96000, 32), 'Unexpected WAV format')
    samples = list(struct.iter_unpack('<ff', chunks[b'data']))
    require(len(samples) == 480000, 'Expected five seconds')
    require(all(math.isfinite(x) for f in samples for x in f), 'Non-finite audio')
    def ac_rms(begin, end):
        values = [f[1] for f in samples[int(begin*rate):int(end*rate)]]
        mean = sum(values)/len(values)
        return math.sqrt(sum((v-mean)**2 for v in values)/len(values))
    active, tail = ac_rms(1, 3.5), ac_rms(4.5, 5)
    require(active > 1e-5, 'Held note is silent or DC')
    require(tail < active * 0.01, 'Note-off did not silence the envelope')
    print(f'PASS 101: MIDI pitch/velocity/gate, envelope, loop metadata; held AC={active:.9g}, tail AC={tail:.9g}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', help='Console --snapshot-prefix')
    parser.add_argument('--verify-101', metavar='WAV', help='Check default note 60, velocity 100, channel 1')
    parser.add_argument('--disassemble-prefix', help='Write held P:0..0x7ff packed words and disassembly')
    args = parser.parse_args()
    states = {p: read_state(f'{args.prefix}-{p}.state') for p in ('ready', 'held', 'released')}
    print(json.dumps({p: describe(s) for p, s in states.items()}, indent=2))
    if args.verify_101:
        verify_101(states, args.verify_101)
    if args.disassemble_prefix:
        binary = Path(args.disassemble_prefix + '.dsp')
        binary.write_bytes(b''.join(states['held']['P', a].to_bytes(3, 'big') for a in range(0x800)))
        result = subprocess.run(['dsp56300-disasm', '-f', 'u24be', '-s', '0', '-n', '4096', str(binary)],
                                check=True, capture_output=True, text=True)
        Path(args.disassemble_prefix + '.asm').write_text(result.stdout)


if __name__ == '__main__':
    main()
