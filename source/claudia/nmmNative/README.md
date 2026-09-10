# Native Nord Micro Modular 101

Isolated implementation and verification work for `101.pch`, using the current
OS 3.03b emulator as reference and a bare Teensy 4.1 as the current embedded target.
The earlier Teensy 3.6 test passed the digital reference but missed the 96 kHz
throughput budget. Its environments and measurement artifacts are retained;
the default environment now targets the subsequently connected Teensy 4.1.
There is no external codec and no physical Nord reference. This directory does
not alter the production emulator or its firmware.

## Layout

- `core/`: portable integer DSP kernels; no instruction decoder or emulator link.
- `reference/`: desktop-only firmware capture and DSP differential executables.
- `mcu/`: recovered note/velocity transport, smoothing and coefficient evidence.
- `tools/`: provenance capture and strict artifact comparisons.
- `teensy/`: PlatformIO firmware and USB diagnostic monitor.
- `build/`: ignored local captures, extracted data, toolchain and test reports.

## Build the reference and native tests

An existing configured emulator build is required for reference tools. The native
CMake project refreshes `nmmLib` before linking its current static libraries;
`NMM_EMULATOR_BUILD` selects that build. Both builds must use compatible compiler,
architecture and ABI settings. The archive integration currently targets Unix
Makefile-style emulator builds, as used by this macOS workspace.

From the repository root on this machine:

```sh
cmake -S source/claudia/nmmNative -B source/claudia/nmmNative/build/desktop \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.12
cmake --build source/claudia/nmmNative/build/desktop -j 4
ctest --test-dir source/claudia/nmmNative/build/desktop --output-on-failure
```

The new native code uses normal C++17 optimization without fast-math. Native
test assertions remain enabled in Release. Emulator libraries retain their
existing reference build configuration. For core-only builds, configure with
`-DNMM_BUILD_REFERENCE=OFF`; this does not validate against firmware.

## Reproduce the 101 reference

Set `NMM_FIRMWARE` to the locally decoded 354,016-byte OS 3.03b image. Its required
SHA-256 is `0ccbffa696f4aa65baa53768695dc02b3afa61ce572006b532cf278515759c3c`.
No firmware download is part of these tools.

```sh
python3 source/claudia/nmmNative/tools/pin_reference.py "$NMM_FIRMWARE"
source/claudia/nmmNative/build/desktop/nmm101Capture "$NMM_FIRMWARE" \
  source/claudia/nmmNative/build/reference-a nord-micro-modular/patches/101.pch 4096
source/claudia/nmmNative/build/desktop/nmm101Capture "$NMM_FIRMWARE" \
  source/claudia/nmmNative/build/reference-b nord-micro-modular/patches/101.pch 4096
python3 source/claudia/nmmNative/tools/diff_capture.py \
  source/claudia/nmmNative/build/reference-a source/claudia/nmmNative/build/reference-b
```

This captures 96,000 warmup frames, 4,096 note-on frames (note 60, velocity 100),
and 8,192 release frames at 96 kHz. Master volume is explicitly 100. The scheduler
uses 64-frame cooperative grants, checked linking and 16-instruction JIT blocks.
The release window is a transient capture, not a claim that this patch's long
release has reached silence.

`audio.int18` contains the exact signed-18-bit decoded DAC values in 32-bit
little-endian containers. The power-of-two float decode is inverted exactly and
checked; `audio.nmma` separately preserves its IEEE-754 bits. P/X/Y snapshots cover
`0..0x1ffff`, not every configured external-memory address. `.state` files contain
the existing diagnostic register/state dump, not a full restartable machine image.
MCU snapshots cover the two declared regions. Missing artifacts, hash failures,
event overflow or differing data reject comparison.

These complete-emulator repeatability captures are separate from isolated
module differential tests. A module pass does not prove complete native patch
timing or editor compatibility.

## Teensy build and diagnostics

```sh
pio run -d source/claudia/nmmNative/teensy -e teensy41_native_101
pio run -d source/claudia/nmmNative/teensy -e teensy41_native_101 -t upload
```

PlatformIO uses `build/pio-core` within this new directory, not the user's global
installation. The platform is pinned to Teensy 6.0.0, with the tested Arduino
framework 1.62 and GCC 15.2.1. The Teensy 4.1 target runs at stock 600 MHz and exposes USB MIDI
plus serial. Uploading replaces the connected board's current sketch.

`teensy/monitor.py` uses pyserial, already available in PlatformIO's Python
environment. It requires a unique Teensy serial port or an explicit `--port`,
collects bounded JSON reports, and fails if no report arrives. The present
bring-up image reports its stage explicitly; transport/timer results must not
be interpreted as full 101 fidelity or real-time audio validation.

See [the implementation plan](../../../doc/nord-101-teensy-plan.md) for the
remaining acceptance gates. Hardware I/O and physical-Nord equivalence are not
claimed by digital emulator agreement.

## Native graph and hardware oracle

`Graph101` implements the selected oscillator, filter, envelope and stereo
output path of the fixed patch. Constant coefficient calculations are cached
between pitch/velocity changes; phase, filter and envelope state still advance
at their original rates. Call `invalidateCoefficients()` after diagnostic
writes to patch coefficients or lookup tables. General patch loading and editor
parameter conversion are not implemented.

The complete graph differential covers 233,472 frames: all 128 notes at each
of four velocities, gate edges, and a final long release. Every X/Y word is
compared against the captured DSP program, using both interpreter and JIT.
The same board runner checks 912 block audio/state hashes on the target.

Generate the local board fixtures before a fresh PlatformIO build:

```sh
python3 source/claudia/nmmNative/mcu/generate_101_reference_state.py \
  source/claudia/nmmNative/build/reference-a \
  source/claudia/nmmNative/build/reference-a/nord101_reference_full_state.generated.h
source/claudia/nmmNative/build/desktop/nmmFilterF92Differential --emit-board \
  source/claudia/nmmNative/build/fixtures/filter_f92_vectors.h
source/claudia/nmmNative/build/desktop/nmmGraphDifferential \
  source/claudia/nmmNative/build/reference-a --emit-board \
  source/claudia/nmmNative/build/fixtures/graph101_vectors.h
```

USB serial commands: `g` runs the reference oracle and measures timing; `s`
steps 64 samples while realtime mode is disabled; `?` reports status. The timer
starts only after reference agreement and the timing margin pass. The `monitor.py`
options `--command g --require-graph-pass --target teensy41` enforce the hardware
reference check. USB MIDI note-on/off is supported; no external codec is needed
for these digital tests.

The resident master/DAC conversion has a separate exact differential test.
Full MCU delivery latency, ESSI buffering, arbitrary patch loading and analog
I/O equivalence remain outside the module-graph verification claim.
