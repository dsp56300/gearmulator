"""nmrack: decode the Rack ROM and extract MCU-verified DSP upload blocks.

Requires capstone; writes only to the explicitly supplied output directory.
MCU addresses are bytes, DSP addresses are words. Runtime begins at file c800.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

RAW_HASH = '14eb3479fa8815da967bf636915d46a9a125d137b357edb0e2eae3d34f6ebdb3'
DECODED_HASH = 'd9b199f27f573fdf7cd985fbd33cb9e60893a08912ab5d1bb562c77c663c997e'
BASE, OFFSET, SIZE = 0x100000, 0xc800, 0x5dfe0
BLOCKS = [
    ('loader', 0x1445a8, 0x27, 0x100),
    ('resident-first', 0x144644, 0x175, 0),
    ('resident-middle', 0x144c18, 0x175, 0),
    ('resident-last', 0x1451ec, 0x175, 0),
    ('resident-tail', 0x1457c0, 5, 0x200),
    ('prefix-first', 0x1457d4, 0x23, 0),
    ('prefix-middle', 0x145860, 0x23, 0),
    ('prefix-last', 0x1458ec, 0x28, 0),
    ('suffix-other', 0x14598c, 0xb, 0),
    ('suffix-last', 0x1459b8, 0xb, 0),
    ('serial-loader', 0x1459e4, 0x2c, 0x100),
]


def swap(v):
    return ((v & 0x55) << 1) | ((v & 0xaa) >> 1)


def decode(data):
    digest = hashlib.sha256(data).hexdigest()
    if digest == DECODED_HASH:
        return data
    if digest != RAW_HASH:
        raise ValueError('Unsupported Rack ROM SHA-256: ' + digest)
    result = bytes(swap(v) ^ ((17 * (i + 1)) & 255) for i, v in enumerate(data))
    assert hashlib.sha256(result).hexdigest() == DECODED_HASH
    assert bytes(swap(v ^ ((17 * (i + 1)) & 255)) for i, v in enumerate(result)) == data
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_020
    data = decode(args.firmware.read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)
    app = data[OFFSET:OFFSET + SIZE]
    (args.output / 'rack-decoded.bin').write_bytes(data)
    (args.output / 'rack-application.bin').write_bytes(app)
    manifest = dict(decoded_sha256=DECODED_HASH, application_offset=OFFSET,
                    application_base=BASE, application_size=SIZE,
                    active_dsp_count_address=0x1ab91c, host_table=0x15bd68,
                    host_ports=list(struct.unpack_from('>8I', app, 0x5bd68)), blocks=[])
    for name, address, count, pc in BLOCKS:
        words = struct.unpack_from(f'>{count}I', app, address - BASE)
        if any(w > 0xffffff for w in words):
            raise ValueError(f'{name}: word exceeds 24 bits')
        packed = b''.join(w.to_bytes(3, 'big') for w in words)
        path = args.output / (name + '.dsp')
        path.write_bytes(packed)
        asm = subprocess.check_output(['dsp56300-disasm', '-f', 'u24be', '-c', 'never', '-s', hex(pc), str(path)], text=True)
        path.with_suffix('.asm').write_text(asm)
        manifest['blocks'].append(dict(name=name, mcu_address=address,
            file_offset=address-BASE+OFFSET, words=count, disassembly_origin=pc,
            sha256=hashlib.sha256(packed).hexdigest()))
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    cs.skipdata = True
    for name, start, end in [('startup', 0x109754, 0x109870),
                              ('dsp-upload', 0x10bad4, 0x10c4b0),
                              ('board-init', 0x100264, 0x100500),
                              ('board-signals', 0x118dea, 0x118f0a)]:
        listing = '\n'.join(f'{i.address:06x}: {i.bytes.hex():24} {i.mnemonic:10} {i.op_str}'
                            for i in cs.disasm(app[start-BASE:end-BASE], start))
        (args.output / (name + '.m68k.asm')).write_text(listing + '\n')
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
