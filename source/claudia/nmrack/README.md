# nmrack

Separate, experimental Nord Modular Rack emulator: one MCU and four DSP56303
instances. Rack-specific implementation, firmware analysis and future audio/plugin
integration live here, not in the Micro Modular emulator. The original firmware
file remains at its user-supplied location.

Current implementation is an independent `nmrackLib` hardware library and a
headless `nmrack` executable and standalone `nmrackPlayer`, not yet a plugin. It reuses the Micro
Modular patch parser, SHA-256 implementation and header-only
flash/editor-port models without modifying them; those peripheral models remain
provisional for the Rack.

**Current acceptance status:** the serial DAC transaction path passes the strict
four-output tone test, including buffer/JIT consistency and varied patch-load
timing. The callback-isolated standalone player supports native patch compilation
and timestamped MIDI controls. Physical analog latch timing remains unvalidated;
the CoreAudio output path has passed muted device tests.

## Build and run

From the repository root, using an existing configured build directory:

```sh
cmake -S . -B build/nmm -Dgearmulator_BUILD_NMRACK=ON
cmake --build build/nmm --target nmrack -j6
build/nmm/source/claudia/nmrack/nmrack \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin'
```

The build-directory name is incidental; `nmrack` is an independent target and does
not require enabling the Micro Modular target. Optional positional arguments are
machine frames (default 480000) and a cumulative execution-step budget (default
450000000). `--trace` enables host-word tracing. Exit success requires all four
residents, padding/tail, initialization tables and initial gains to match, plus
a recurring MCU application event loop. Incomplete boots and exhausted budgets fail.

## Diagnostic tone and audio-target execution

```sh
build/nmm/source/claudia/nmrack/nmrack \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --tone --experimental-chain --mix-wav build/nmrack-tone.wav
python3 source/claudia/nmrack/scripts/check_tone.py build/nmrack-tone.wav
```

The firmware allocates, connects and compiles an oscillator/output patch. Two
experimental serial chains carry its data through DSP0 → DSP1 → DSP2 → DSP3.
`--diagnostics build/nmrack-tone` additionally saves bounded serial traces and
DSP P/X/Y snapshots. `--unlinked` selects the reference JIT for comparison.

The WAV contains one second of four-channel, 96-kHz **diagnostic mixer output**.
It taps the final DSP's completed native sample program, decodes its low 18 bits
using the Micro Modular-derived DAC convention, and does not model physical DAC
latch edges. A measured capture has a 330-Hz tone with AC RMS 0.03661. It emerges
on channel 4 rather than the intended output, so physical serial phase and channel
mapping remain unvalidated. This is not evidence of correct four-output routing.

`Hardware::renderDiagnosticMix` is a worker-only output-buffer-target API. It
drains exactly the requested frames, retains surplus in a bounded queue, and
advances the machine in grants of at most 64 frames. Checked JIT links retain
16-instruction blocks and return for peripheral/sample/owner deadlines. Neither
firmware execution nor lazy compilation belongs on a host audio callback.

## Word-edge transport

```sh
build/nmm/source/claudia/nmrack/nmrack \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --tone --word-serial --diagnostics build/nmrack-words --mix-wav build/nmrack-words.wav
python3 source/claudia/nmrack/scripts/check_tone.py build/nmrack-words.wav
python3 source/claudia/nmrack/scripts/check_words.py build/nmrack-words-words.csv
```

`--word-serial` implies experimental chaining and replaces the paired-word FIFO
with immediate word/FS delivery. It captures TX before DMA refills the register,
advances receivers to the source edge, then latches RX before triggering receive
DMA. Receiver execution first catches its upstream source up to the same target.
The legacy `--experimental-chain` frame path remains available for comparison.
Only `nmrack` opts into the new shared ESSI hooks; other emulators keep their
existing frame-based behavior.

This mode holds each ESSI inactive while its corresponding port is entirely GPIO.
That startup gating fixes the observed nine-word receive-buffer rotation: a
330-Hz tone now reaches **diagnostic output 2**, AC RMS approximately 0.03661,
with no AC on the other three outputs. A 9600-sample trace shows the signal at
`X:0x6c1` on every link and zero receive-address discontinuities. The remaining
receiver lead is bounded JIT overshoot (observed maximum below 0.08 samples), not
an exact bit-clock simulation.

Diagnostics include `-words.csv` (word, slot, GPIO and receiving DMA snapshots),
`-qs.csv` (MCU QS changes from boot), and `-board.csv` (DSP port/RX-DMA changes
observed at block boundaries from boot). These are bounded, worker-only captures.
Frame-sync polarity/bit timing, cross-port clock wiring, physical DAC latch edges
and four-output routing remain unvalidated. The WAV still taps mixer memory.

## Serial DAC output and real-time playback architecture

```sh
cmake --build build/nmm --target nmrack nmrackDacTests -j6
build/nmm/source/claudia/nmrack/nmrack \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --output-tones --validate-dac --dac-wav build/nmrack-dac.wav
build/nmm/source/claudia/nmrack/nmrackDacTests \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin'
```

`renderAudio` assembles four newly loaded ESSI TX words per sample. Audio values
come from TX, never from mixer memory. It checks DMA source pair/bank provenance,
rejects duplicate/incomplete frames and bad cadence, decodes signed 18-bit payloads,
and retains bounded render surplus. `--validate-dac` additionally compares each
payload with its DMA source on the worker. It is a **payload/provenance check**,
not the strict four-output spectral acceptance test; run `nmrackDacTests` for that.

The native four-output module writes its input signals to mix locations in order
`[2,1,4,3]`. The DAC transaction model reverses each pair to expose logical output
order. Unlike the old diagnostic WAV, the serial DAC WAV therefore uses that
firmware-derived logical channel order. Analog jack wiring/latch edges remain
unmeasured. The model distinguishes newly loaded words from held-register repeats
using TX status, which is emulator-side transaction information, not a signal
available to a physical DAC. It must not be described as a pin-accurate DAC model.

`Playback` owns a booted `Hardware` on one producer thread. Its `consume` method
only copies from a fixed lock-free SPSC queue and fills shortages with zero; it
does not execute firmware/JIT, allocate, lock, wait, log, or throw. Production runs
use 128-frame worker requests and a configurable queue target. Processor
deadlines stay bounded. Board/serial tracing is off unless explicitly requested.
`Hardware` remains **96 kHz**. `Playback` converts on the worker with four persistent
band-limited libresample filters, bypassed at 96 kHz, and queues host-rate frames.
Control/patch changes must not access `Hardware` concurrently with its worker.

### Standalone device player

Build `nmrackPlayer` in a JUCE-enabled configuration (`gearmulator_BUILD_JUCEPLUGIN=ON`,
`gearmulator_BUILD_NMRACK=ON`, `gearmulator_BUILD_NMRACK_PLAYER=ON`). For the existing
local UI build:

```sh
cmake -S . -B build/nmm-ui -Dgearmulator_BUILD_NMRACK=ON
cmake --build build/nmm-ui --target nmrackPlayer -j6
build/nmm-ui/source/claudia/nmrack/nmrackPlayer_artefacts/Release/nmrackPlayer --list-devices
build/nmm-ui/source/claudia/nmrack/nmrackPlayer_artefacts/Release/nmrackPlayer \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --rate 48000 --buffer 256 --output-tones --monitor mix --seconds 30
```

Default patch: native 330-Hz oscillator on logical output 2. `--output-tones`
loads the four-output validation patch. Default gain is 0.2; start with low speaker
volume. Omit `--seconds` to run until Ctrl-C. No ROM is bundled.

- `--device NAME` and `--backend NAME` select an exact listed output; no audio input opens.
- `--monitor 12` (default) or `34` monitors one pair; `mix` averages the two pairs;
  `four` preserves all four logical outputs and requires four device channels.
- `--map 1,2` or `--map 1,2,3,4` sets unique, one-based physical destinations.
  Unused channels are silenced. `--gain` accepts 0..1.
- `--simulate` uses the identical callback and worker with a paced synthetic device;
  it does **not** prove physical-device playback. Supports arbitrary integer buffer
  sizes from 16 to 2048 and output rates from 32 to 192 kHz.

The player uses the device's negotiated format and prefills four callback blocks
(at least 512 frames, rounded to 128). At 48 kHz/256 this target is 21.33 ms;
reported device output latency and resampler delay are additional, so queue target
is not a measured end-to-end latency. Shutdown stops device callbacks before joining
the worker. Device failure/format change silences output and requires restart.
The control thread detects a missing callback heartbeat. Final statistics report
missing frames, queue underrun events, worst worker render time (including prefill),
whole-process CPU cores used, and backend xruns (-1 means unavailable).
Queue underruns count deficient 128-frame chunks, not necessarily device callbacks.
Patch/MIDI control handling is described below.

Local validation (2026-09-10, Release arm64): all four existing regression suites
passed, plus resampler block-invariance/channel-isolation/DC/passband tests at
44.1/48/96/192 kHz, 30-kHz anti-alias rejection at 48 kHz, and monitor/gain tests.
A **muted** MacBook Pro Speakers CoreAudio run at 48 kHz/256 lasted 10 seconds with
zero underruns, missing frames, or device xruns (0.83 average process CPU cores).
A 30-second four-output simulation at 48 kHz/256 also had zero underruns/missing
frames (0.79 CPU cores, 3.36-ms worst 128-host-frame worker render).
A 10-second 44.1-kHz/127-frame simulation with reversed four-channel mapping
also passed with zero underruns/missing frames (0.78 CPU cores).
These are local test-patch measurements, not a guarantee under arbitrary patches
or host load, nor an audible/analog-output validation.

### Play patches with MIDI

```sh
build/nmm-ui/source/claudia/nmrack/nmrackPlayer_artefacts/Release/nmrackPlayer --list-midi
build/nmm-ui/source/claudia/nmrack/nmrackPlayer_artefacts/Release/nmrackPlayer \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --patch nord-micro-modular/patches/FourVoices.pch --midi-input 'YOUR MIDI INPUT' \
  --rate 48000 --buffer 256
```

Use MIDI channel 1 for slot A. Notes, velocity, pitch bend, channel controllers
and all-notes-off enter the MCU's cycle-timed SCI receiver; the firmware allocates
voices and performs synthesis. No host oscillator/envelope substitutes are used.
`--midi-demo` generates alternating four-note chords without a keyboard.
`--voices N` overrides requested polyphony (1..32); with this override the demo
plays N notes, exercising native voice stealing if allocation is lower. For the
FourVoices fixture, requesting 32 allocates 28 voices, seven on each DSP. With its
default four voices, the firmware places all four on DSP 0; the audio still passes
through the four-DSP output chain. Startup reports actual allocation and DSP mask.

Patch text is parsed on the control thread with the existing Micro Modular parser.
The Rack worker invokes mapped native MCU allocation/parameter/compiler helpers
and prefills audio **before** starting the device. This is an initial patch loader,
not gapless hot replacement or a complete editor SysEx protocol implementation.
Morph mappings/keyboard morph assignments are explicitly rejected until ported;
unsupported patches must not be assumed to work. Restart the player to change
patches. Module custom data must match the Rack's firmware schema.

`Playback::submit(Command)` is the control/MIDI producer API for timestamped MIDI
and live module parameters. Timestamps are native 96-kHz output frames relative
to playback initialization; `nativeFrame()` returns the producer cursor, already
ahead of audible output. Live MIDI uses arrival-time stamping at that cursor;
it does not preserve timestamps from an external sequencer. Late submissions are
handled on the next grant and counted. MIDI is serialized at approximately wire
speed, so simultaneous chord notes do not arrive at the MCU simultaneously.

The fixed 256-command inbox supports multiple control producers. Producers use a
short mutex-protected insertion; the worker only tries the lock and renders on
contention. **The audio callback never touches this inbox.** Worker renders split
at scheduled events. Parameter changes wait for the MCU event-loop safe point and
use both storage and DSP coefficient-broadcast helpers. Excessive control traffic
can still exceed real-time headroom; no arbitrary-rate automation guarantee is made.
On inbox overflow, submission fails, a counter increments, and an out-of-band
panic discards queued events, releases sustain and sends all-notes-off on all
channels. `Playback::allNotesOff()` requests the same bounded recovery.

Native MIDI regressions cover note release, velocity, pitch bend, mapped CCs,
live parameters and 28 simultaneous voices spanning all four DSPs. A paced
48-kHz worker test checks scheduled note/parameter changes and zero-velocity
note-off, with zero underruns or late commands. Native note onset was about
1.43–1.45 ms; scheduled host-output onset was about 1.35 ms after its target.
These exclude queued-audio/device latency and are **not** measured keyboard-to-jack
latency. Tests are registered when `NMRACK_TEST_FIRMWARE` and `NMRACK_TEST_PATCH`
point to the local ROM and FourVoices fixture. Firmware is never bundled.

**Polyphony is not the same as real-time capacity.** A three-minute muted
48-kHz/256 CoreAudio test with 28-note chords allocated voices across all four
DSPs, but saturated one process CPU core and missed 508928 of 8638976 host frames
(5.9%; 3976 deficient callback chunks). Device xruns, rejected commands and late
commands were zero. This load is **not real-time** on the tested machine; increasing
the queue cannot fix sustained throughput below 1x. Use lower polyphony and check
the final underrun counters. Larger patches need further worker/JIT optimization.
These initial long runs used a three-block queue. The four-voice run used 0.80 CPU
cores but missed one 128-frame chunk in 180 seconds; the default queue was then
increased to four blocks for additional host scheduling margin, not to mask the
28-voice throughput deficit.

The final **180-second four-voice** retest with the new four-block target passed:
8639232 frames, zero underruns/missing frames, zero device xruns, and zero late or
rejected commands; average process CPU was 0.793 cores and worst worker render
9.857 ms. This used muted MacBook Pro Speakers output at 48 kHz/256, Volt 476 MIDI
input open, and the built-in MIDI sequence (`--patch .../FourVoices.pch --midi-demo`).
See `build/nmrack-midi-four-buffered180.log`. The external keyboard itself was not
played in this test. This establishes the tested default-patch playback case,
not a guarantee for every patch, polyphony setting, device or host load.

```sh
build/nmm/source/claudia/nmrack/nmrack \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin' \
  --tone --word-serial --realtime-test 3
```

This is a wall-clock-paced callback simulation, **not sound-device playback**.
It reports underruns/missing frames and fails on underruns or worker errors. A
local three-second single-tone run and ten-second four-oscillator run had zero
underruns/missing frames. These are smoke tests, not a sustained
real-time guarantee.

The four-tone acceptance fixture uses native firmware oscillators at nominal
220, 277.18, 349.23 and 440 Hz. Payload, cadence, channel-frequency/isolation and
linked/unlinked / buffer-size checks pass. Patch loading after additional 1- and
17-sample delays is also checked. The earlier receive-buffer rotation was fixed
by instruction-precise JIT blocks in initialization/resume (`P:9e..169`) and the
sample prefix (`P:175..19c`). Normal arithmetic retains 16-instruction blocks;
precise blocks use deadline-checked native links where enabled. There is no
compensating output shuffle, firmware patch, or mixer-memory fallback.

## Firmware investigation

`scripts/inspect_firmware.py` accepts the firmware path and an output directory.
It requires Python Capstone and `dsp56300-disasm` on PATH. It produces decoded MCU
images, role-specific DSP disassemblies and a manifest of offsets and hashes.
Generated firmware artifacts should remain in an ignored build directory.

The supplied 512-KiB file still needs adjacent-bit swapping followed by the
position-dependent XOR. The synth application starts at file offset `0xc800`,
mapped to MCU address `0x100000`; the earlier image at `0x800` is maintenance code.
The emulator currently bypasses that maintenance boot path.

The MCU probes the optional expansion hosts and selects four DSPs. Each has its
own HDI08, memory, JIT, timers, DMA and two ESSI ports. The initial resident programs
have first/middle/last roles. A five-second machine-time run reaches the recurring
MCU event loop at `0x100e6e`; all five initialization tables on each DSP are checked.

Execution is bounded by a shared frame target with small lagging-processor grants
and host-access catch-up, informed by the multi-DSP reference emulator. DSP JIT
blocks remain bounded at 16 instructions with dynamic fast-interrupt support;
word mode uses one-instruction blocks in timing-critical control regions.
Physical serial interconnect, clock phase and codec routing are not yet validated.
Without `--experimental-chain`, ESSI receives zeros. The external
96-kHz sample interrupt is explicitly a Micro Modular-derived approximation.
No production real-time audio claim is made yet. See [INVESTIGATION.md](INVESTIGATION.md)
for native helper addresses, fixes and the known timing limitations.

## Tests and remaining work

```sh
cmake --build build/nmm --target nmrackTests nmrackHardwareTests -j6
build/nmm/source/claudia/nmrack/nmrackTests
build/nmm/source/claudia/nmrack/nmrackHardwareTests \
  'nord-micro-modular/Nord Modular Rack/nord-rack.bin'
```

`NMRACK_TEST_FIRMWARE` is an optional CMake FILEPATH for registering hardware tests
with CTest. No firmware is embedded or downloaded. Tests cover timestamped serial
delivery, overflow, disabled receivers, circular RX DMA, boot, native compilation,
variable output targets, and bit-identical linked/unlinked diagnostic audio. The
existing Micro Modular peripheral regression also passes with the DMA addition.

Word-mode tests additionally require the tone on diagnostic output 2, no AC on
other channels, approximately 330 Hz, bounded receiver lead, and bit-identical
samples for linked/unlinked JIT and fixed 7/128 or irregular output targets.

Remaining: physical board clock/FS phase, validated DAC routing and
ADC input, complete patch/editor import/export (including morphs), gapless patch
replacement, plugin integration, and broader real-time/device stress tests. The
headless tone is not completion of those milestones.
