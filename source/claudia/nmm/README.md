# Nord Micro Modular: headless firmware bring-up

The native 68k firmware boots, compiles patches and allocates MIDI voices; the
DSP JIT executes the generated oscillator, filter, envelope and mixer code.
Four independent voices now pass pitch, release, sustain and stealing checks.
The output decodes the board's 18-bit DAC serial format, with master volume
controlled by the original firmware. No host oscillator or makeup gain is used.
See the [current MCU/DSP investigation](../../../nord-micro-modular/analysis/four-voice-audio-path.md)
for evidence, measurements and remaining hardware limitations.

`nmmLib` owns the machine; `nmmTestConsole` drives bounded boot/compile/render runs. `nmmJucePlugin` now supplies a shared-framework VST3/standalone with the photographed panel skin and a buffered emulator worker. See [plugin/UI build and limitations](nmmJucePlugin/README.md).

## Build

Initialize the repository's pinned submodules. The DSP submodule currently has local fixes for a null MMU fallback, absolute-short program-memory MOVE in both execution engines (including JIT invalidation), and ESSI/DMA behavior. They are also preserved in `patches/dsp56300-nmm.patch` so a parent-repository checkout can reproduce them. The MCU receive-timing change is preserved in `patches/mc68k-nmm.patch`. On clean pinned submodules, apply both with:

```sh
git -C source/cpu/dsp56300 apply ../../claudia/nmm/patches/dsp56300-nmm.patch
git -C source/cpu/mc68k apply ../../claudia/nmm/patches/mc68k-nmm.patch
```

Do not apply again to a working tree that already contains the fixes.

```sh
cmake -S . -B build/nmm -DCMAKE_BUILD_TYPE=Release \
  -Dgearmulator_BUILD_JUCEPLUGIN=OFF \
  -Dgearmulator_SYNTH_NORDMICROMODULAR=ON \
  -Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF \
  -Dgearmulator_SYNTH_VAVRA=OFF -Dgearmulator_SYNTH_XENIA=OFF \
  -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_JE8086=OFF
cmake --build build/nmm --target nmmTestConsole nmmTests nmmPeripheralTests nmmHardwareTests
ctest --test-dir build/nmm -R '^nmm(Tests|PeripheralTests)$' --output-on-failure
```

On the tested Apple Silicon machine, configuration additionally needed `-DCMAKE_OSX_ARCHITECTURES=arm64 -DXCODE_VERSION=99` because this repository's Xcode detection fails with only Command Line Tools installed. The latter is a configuration workaround, not the installed Xcode version. Use the JIT: the pinned interpreter still lacks DO FOREVER.

## Run

Supply the locally decoded OS v3.03b firmware, exactly 354016 bytes with SHA-256 `0ccbffa696f4aa65baa53768695dc02b3afa61ce572006b532cf278515759c3c`. Firmware is neither bundled with this target nor downloaded.

```sh
build/nmm/source/claudia/nmm/nmmTestConsole/nmmTestConsole \
  --firmware '/path/to/Nord Micro Modular OS v3.03b decoded firmware.bin' \
  --output build/nmm/tone.wav
python3 source/claudia/nmm/validate_tone.py build/nmm/tone.wav
```

`--boot-only` stops at OS application initialization. `--budget` sets a CPU instruction limit for each boot, firmware call or render segment (default 30000000). `--trace path` records host transactions and diagnostic state.
`--snapshot-prefix path` writes ready/held/released state snapshots outside
rendering. `--benchmark --block-size 128` measures small-block rendering,
including the cold first block. `--note`, `--velocity`, and `--channel` select
the MIDI probe (defaults 60, 100, 1). `--voices 4` overrides the requested
allocation; `--chord 60,64,67,71` probes overlapping notes. Failed runs return nonzero and print the last milestone and CPU/DSP PCs.

`--patch nord-micro-modular/patches/BasicOsc.pch` is the smallest external-path
oscillator regression. Its output is free-running and does not prove MIDI
voice handling. `101.pch` now exercises a sustained envelope and note release;
`SimpleSqr1.pch` produces a short note because its sustain is zero. The console
sends note-on, renders four seconds, sends note-off and renders one second.
It checks both sustained and short attack AC RMS, retaining failed WAVs.
`validate_tone.py` checks periodicity for the internal/BasicOsc fixture.

For the stronger `101` regression, render with `--snapshot-prefix /tmp/voice`,
then run:

```sh
python3 source/claudia/nmm/inspect_voice.py /tmp/voice \
  --verify-101 /path/to/audio.wav --disassemble-prefix /tmp/voice-program
```

This checks CPU readiness, MIDI pitch/velocity/gate, the generated envelope,
JIT loop metadata, held-note audio and release silence. Disassembly uses the
installed `dsp56300-disasm` and writes only offline artifacts.

`FourVoices.pch` is an authored, four-voice sine/ADSR/stereo regression. Run the
firmware integration tests with:

```sh
build/nmm/source/claudia/nmm/nmmTestConsole/nmmHardwareTests \
  '/path/to/decoded firmware.bin' nord-micro-modular/patches/FourVoices.pch
```

`SimpleSynth02.pch` now boots and applies its saved morphs, including the filter
cutoff that otherwise leaves it nearly silent. The firmware allocates two
voices to this larger patch. `Gong01.pch` and the older 2.10-format
`BDrm.pch` also render. The loader applies key/velocity ranges, requested
polyphony, pitch-bend range, retrigger settings, morph mappings/positions and
keyboard morph assignments. Knob mappings support module parameters and morphs.
Portamento, octave shift, layout separator, custom module data and MIDI CC
mappings are also applied through the firmware.

The emulator worker buffers rendering for realtime playback. Cold JIT blocks
can miss a 128-frame deadline in isolation; the plugin now buffers at least
1024 native frames (10.67 ms), increasing for large host blocks. See the
[latency/loading measurements](../../../nord-micro-modular/analysis/latency-loading.md).
This is measured realtime throughput, not a guarantee for every patch or host workload.

## Firmware evidence and provisional board model

See the local `nord-micro-modular/analysis/LOADING-MAP.md` and `cpu-linear.asm`.
The detailed static inspection is split into
[`68k-reverse-engineering.md`](../../../nord-micro-modular/analysis/68k-reverse-engineering.md)
and [`dsp-system.md`](../../../nord-micro-modular/analysis/dsp-system.md). The
latter records the exact `dsp56300-disasm` invocation, extracted-word hashes,
resident vector map and the provisional ESSI/clock assumptions.

- 68k firmware maps at `0x100000`; entry is explicitly selected because no original boot ROM is available. Boot reaches application entry `0x100abe`; patch readiness additionally waits
  for main-loop `0x100b38`, after the mandatory startup compile that clears MIDI.
- The real CPU writes the `0x205`-word DSP boot image through the host interface. The host verifies it against firmware extraction, then verifies all five initialization table uploads at the startup milestone.
- Scratch RAM at `0x1f0000` and a bounded native-call trampoline invoke module allocation (`0x109b44`), cable insertion (`0x1119a4`), parameter setting (`0x109c4c`) and full compilation (`0x1049c0`). Internal calls restore the CPU register snapshot; they are a bring-up shim, not editor-protocol emulation.
- Firmware extends the resident DO FOREVER through a host write to LA. The shared
  JIT now tracks dynamic LA writes, including MOVEP, and invalidates only blocks
  and cached variants crossing a newly observed endpoint. Loop exits use live
  LF/LA/FV and stack state; no Nord-specific endpoint synchronization remains.
- Cable endpoints use bit 6 to distinguish output ports. The internal fixture uses input module/port, output module/(port | `0x40`), color, reserved. The oscillator and output code are produced by the firmware.
- An AM29F080B command decoder supports identification, byte programming, sector/chip erase and reset. Its 1 MiB array persists per instance in host state v5 (with v4 migration); electrical busy/suspend timing remains unmodeled.
- Clock configuration uses an inferred 12.288 MHz external clock, yielding
  82.944 MHz after the firmware PLL and 96 kHz stereo frames from ESSI CRA.
  Timer0's period independently agrees. The physical IRQD connection is still
  approximated by ESSI1 frame callbacks. Stereo input now runs through ESSI/DMA; converter filtering and physical pin timing remain approximate.
- TX0/slot0 on each ESSI feeds one AD1865 channel. The serial word's low 18 bits
  are signed audio; the firmware's volume table and 0x155 bias establish this
  format. Plugin Volume drives the native table/ramp. The plugin retains an
  approximate unity-gain 10 Hz DC blocker for output coupling.
- Unknown CPU bus accesses and out-of-range instruction fetches fail explicitly. There is no general firmware compatibility claim beyond the pinned image.

The [PC UART / Web MIDI integration](../../../nord-micro-modular/analysis/editor-midi.md)
now runs discovery, patch queries, live edits and uploads through the native MCU.
Plugin state preserves native working patches separately from saved bank entries;
New/Open and live edits do not overwrite saved locations. Explicit Store writes
the chosen memory location. Remaining work includes physical
analog ADC/filter and panel behavior, remaining historical PCH variants, native
global-settings workflows, physical timer/pin timing and comparison
against recordings from hardware.
See the current investigation for the distinction between proven digital paths
and the board model's assumptions.

## DSP runtime regression matrix

```sh
cmake --build build/nmm --target nmmPatchRegressionTests
build/nmm/source/claudia/nmm/nmmTestConsole/nmmPatchRegressionTests \
  '/path/to/decoded firmware.bin' nord-micro-modular/patches
```

This covers all six supported fixtures, native graph replacement and restoration,
master mute, serial clock accounting and foreground control progress. Shared JIT
unit tests cover changing counted/forever endpoints, nested loops, subroutine LA
writes and stale single-op caches. DSP timers now use core cycles for this board.
The previous morph residual was a measurement window overlapping the native
smoothing tail; settled output has zero AC without an added gate or gain.
See [DSP runtime follow-up](../../../nord-micro-modular/analysis/dsp-runtime-followup.md).

The mandatory `nmmTimerAudit` CTest covers PWM startup, compare events across
wrap, status-flag clearing, register-write timing and batched advancement against
a one-tick reference. The fixes retain the default clock source for other devices
and use constant-time arithmetic in the runtime. Physical timer output routing
remains provisional; see the runtime follow-up above.


## Realtime runtime follow-up

See [implementation and validation](../../../nord-micro-modular/analysis/realtime-runtime-implementation.md)
for fixed render/input buffers, DMA cycle timing, PWM output edges, IRQ acceptance
measurements, native smoothing, v2.10/custom/controller imports and flash state v4.
`StereoInput.pch` tests external audio without a MIDI trigger. The new
`nmmRealtimeStressTests` target checks paced multi-instance four-voice playback
with live editor edits and reports CPU consumption and queue continuity.

Production plugin/standalone rendering now uses fixed 64-frame audio batches on
the existing hardware worker, with checked JIT linking. No additional DSP thread
is created. Capture remains opt-in and keeps the native batching path active;
instruction-level diagnostics are available on the same worker.

The low-level `Hardware` constructor and console retain explicit reference
configuration for reverse-engineering comparisons: `nmmTestConsole --dsp-window
0` selects instruction-interleaved execution, while `--audio-driven 64
--linked-jit --cooperative` selects the production scheduling configuration.
The optional separate DSP thread remains experimental. Plugin-level tests with
no scheduler override exercise the shipped default.

See [production audio batching](../../../nord-micro-modular/analysis/default-audio-batching.md)
for promotion validation and [the architecture experiment](../../../nord-micro-modular/analysis/audio-driven-scheduler.md)
for the original CPU comparison. [System scheduling and capture](../../../nord-micro-modular/analysis/system-scheduling-capture.md)
describes the bounded capture format and remaining hardware investigation.

### Live module-edit regression

The optional `nmmModuleEditTests` target compiles Animatek-NME's actual module
and cable encoders against the emulator. Enable it in a JUCE-enabled build:

```sh
cmake -S . -B build/nmm-ui -DNMM_EDITOR_SOURCE_DIR=/path/to/Animatek-NME
cmake --build build/nmm-ui --target nmmModuleEditTests -j 4
build/nmm-ui/source/claudia/nmm/nmmTestConsole/nmmModuleEditTests_artefacts/Release/nmmModuleEditTests \
  '/path/to/decoded firmware.bin' /path/to/Animatek-NME/data/modules.xml all
```

Configure `NMM_MODULE_TEST_FIRMWARE` with an absolute firmware path to register
the sweep with CTest (`ctest --test-dir build/nmm-ui -R nmmModuleEditTests`).
Replace `all` with a module type ID to isolate a failure. Append `--reference`
to compare with MCU-led execution; the default uses the plugin's cooperative,
64-frame audio execution and linked JIT settings. No external MIDI ports are
opened. The firmware and editor checkout are local inputs, not downloaded or
included in the test binary.

Each instantiable module is tested in both voice areas on a fresh baseline
patch: native add, a compatible cable where connectors exist, every ordinary
parameter, cable removal, and module removal. Patch section replies and native
parameter values verify the result, and non-finite audio or firmware exceptions
fail the case. Pending volume automation exercises the live-control boundary.
The test mirrors the editor's current empty custom-value list for individual
module creation, so it also exposes differences from full-patch loading. It
prints each failure and returns a nonzero status if any case fails. This sweep
does not establish correctness for every module combination or resource-limit
condition.
