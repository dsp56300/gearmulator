# MCU boundary recovered for patch 101

This directory contains the first bounded native implementation input for the
existing `nord-micro-modular/patches/101.pch` fixture. It is intentionally
separate from the production emulator and from the generated DSP kernels.

The patch parser's first `ModuleDump` area record is `1`, so all five modules
and six cables are in file area 1 (the patch's polyphonic area). The module
rows are `(index, type, x, y)`: `(1,1,0,12)`, `(2,7,0,0)`,
`(3,92,0,6)`, `(4,20,0,14)`, and `(5,4,0,19)`. The extractor keeps that
area and coordinate information instead of treating `x` as an area number.

The contract in `nord101_mcu_contract.h` implements only facts that are
directly supported by OS 3.03b traces or disassembly:

- module types 7 (OscA1), 92 (FilterF1), 20 (ADSR-Env1), and 4 (two outputs),
  with the exact editor bytes in `101.pch`;
- the recovered DSP resource lengths for those four module banks;
- the ready-state coefficient/state words loaded for the fixed 101 editor
  values in the poly binding (`k101PolySampleX/Y`, `k101PolyControlX/Y`),
  plus output parameter 115's exact table result `0x00016605`;
- one fixed voice note transport: pitch uses the firmware sequence
  `((note << 23) - 0x20000000) >> 8`, and gate uses `0x200000`/`0`;
- the complete 128-entry velocity table at `0x14acb2`, equivalently
  `(velocity * 0x200000 + 63) // 127`, yielding velocity 100 -> `0x193265`
  in `X:0x0d`;
- the per-coefficient MCU smoother from `10c958..10c99a`, including 32-bit
  wrap, arithmetic shift, and the strict `-255 < difference < 255` snap;
- the observed fixed 101 DSP state locations (`X:0x0d`, `0x0e`, `0x0f`,
  `0x13`, and `0x14`).

The coefficient arrays are the ready-state words from the reference capture's
poly binding (`00100060006001e00073006a0175`): sample X/Y allocation begins at
`0x60`, control X at `0x73`, and control Y at `0x6a`. Their lengths match the
module bank contracts and provide a fixed native acceptance vector for 101;
they are not advertised as a generic parameter conversion algorithm.

`nord101_reference_state.h` adds the complete sparse X/Y ready-state ranges
needed by the generated graph, including the shared mixer words at X:`0x56`
through `0x5f` and the control/sample allocation through X:`0x85` and Y:`0x72`.
The sample graph starts with `r3=r4=0x60` after the prefix consumes X/Y:`0x5f`;
the generated resident setup sets `n2=0x7bc`, `n5=0x780`, and `r6=0x620`.
The full 0x800-word X/Y build fixture is generated locally with
`generate_101_reference_state.py`; its input snapshots are hash-pinned.

The 101 patch has one requested voice. This code therefore carries one voice
state and does not claim to reproduce the generic allocator, sustain, or
retrigger policy. A velocity-zero note-on is treated as note-off, and notes
above 127 are rejected. Editor parameter conversion is also callback-driven (`109c4c`,
`11ae08`, `106e68`, `106d40`) and is not guessed here.

`extract_101_mcu_deps.py` emits a reproducible JSON dependency report. It
extracts the patch records, validates the pinned firmware and patch hashes,
and records the exact firmware tables used by the pitch/gate helper:

- `0x154468`: the eight host-port pointers;
- `0x14acb2`: 128 four-byte velocity entries used for `X:0x0d`;
- `0x14c3b2`: the master coefficient table, including the 101 output value
  115 (`0x00016605`).

The `0x14acb2` table is preserved as data only. Its surrounding call path
does not yet establish a complete velocity conversion, so a Teensy build must
not replace it with a hand-derived curve. The installed
`dsp56300-disasm` remains the reference for checking the generated DSP side;
the MCU contract does not hardcode generated program addresses.

The standalone contract test can be compiled with C++17 by the parent native
build once root wires it into the project. The second standalone test is the
captured-trace differential check:

```sh
c++ -std=c++17 -Wall -Wextra -Werror nord101_mcu_contract_test.cpp -o /tmp/nord101_mcu_contract_test
/tmp/nord101_mcu_contract_test
c++ -std=c++17 -Wall -Wextra -Werror nord101_mcu_trace_test.cpp -o /tmp/nord101_mcu_trace_test
/tmp/nord101_mcu_trace_test
```

No flashing or hardware mutation is performed by this directory.
