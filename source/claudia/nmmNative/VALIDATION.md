# Native 101 validation — Teensy 4.1

The connected Teensy 4.1 is running the fixed native 101 module graph at a
nominal 96 kHz, with 24 kHz control updates and USB MIDI note-on/off.
No audio codec is attached; outputs are digital graph words and test hashes.

## Recorded results

- Desktop: all 23 CTest cases passed, including interpreter and JIT differential tests.
- Hardware: **912/912 blocks passed**, covering **233,472 frames** with exact audio and full X/Y state hashes.
- Corpus: every MIDI note at velocities 1, 64, 100 and 127; gate transitions; a final long release.
- CPU: 600 MHz; frame budget: 6,250 cycles.
- Offline frame average: 1489.3 cycles; maximum: 3,105 cycles.
- Live callback maximum: 2,712 cycles; reported overruns: **0** across **10,850,189** rendered frames at the final check.
- USB MIDI: note 60 / velocity 100 applied with exact pitch, velocity and gate words; matching note-off cleared the gate. Two messages accepted/applied, zero dropped.
- Portable board runners also passed ASAN/UBSAN.

The board remains in continuous rendering mode after the test, with the note released.

## Scope

This proves agreement with the captured compiled 101 DSP module graph for the
tested corpus. The board runs native C++ integer kernels, not a DSP instruction
emulator. Fixed coefficients and lookup tables come from the local emulator
capture. General patch loading, editor parameter conversion, complete MCU/ESSI
latency, analog I/O and equivalence to a physical Nord are not claimed.
The resident master/DAC conversion passes a separate 10,000-vector DSP differential.

The earlier Teensy 3.6 image passed the same digital oracle, but its measured
throughput before the final optimizations missed 96 kHz. Those measurements do
not characterize the newer optimized code on that board.

## Local evidence

- `build/ctest.log`
- `build/teensy41-upload.log`
- `build/teensy41-graph.json`
- `build/teensy41-midi-on.json`
- `build/teensy41-midi-off.json`
- `build/reference-a/` and `build/fixtures/`

Build and monitor instructions are in [README.md](README.md).
