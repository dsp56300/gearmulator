"""Locate candidate Rack helpers by relocation-masked Micro Modular code.

This is an investigation aid, not an automatic address migration. Inspect every
candidate and its callers before using it. Requires Capstone.
"""
import argparse
import re
from pathlib import Path
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_020
from inspect_firmware import decode, OFFSET, SIZE


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('micro', type=Path)
    p.add_argument('rack', type=Path)
    p.add_argument('addresses', nargs='+', type=lambda s: int(s, 16))
    p.add_argument('--instructions', type=int, default=16)
    args = p.parse_args()
    micro = args.micro.read_bytes()
    rack = decode(args.rack.read_bytes())[OFFSET:OFFSET+SIZE]
    cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_020)
    for address in args.addresses:
        code = list(cs.disasm(micro[address-0x100000:address-0x100000+120], address))
        pattern = bytearray()
        parts = []
        for i in code[:args.instructions]:
            b = bytes(i.bytes)
            mask = [False]*len(b)
            for j in range(2, len(b)-3, 2):
                if 0x100000 <= int.from_bytes(b[j:j+4], 'big') < 0x200000:
                    mask[j:j+4] = [True]*4
            if i.mnemonic.startswith('b') and not i.mnemonic.startswith(('bt', 'bc', 'bs')):
                mask[1:] = [True]*(len(b)-1)
            parts.extend(b'.' if m else re.escape(bytes([v])) for v, m in zip(b, mask))
            pattern.extend(b)
            if i.mnemonic == 'rts':
                break
        matches = [hex(m.start()+0x100000) for m in re.finditer(b''.join(parts), rack, re.DOTALL) if m.start()%2 == 0]
        print(hex(address), 'bytes', len(pattern), 'candidates', ', '.join(matches) or 'none')


if __name__ == '__main__':
    main()
