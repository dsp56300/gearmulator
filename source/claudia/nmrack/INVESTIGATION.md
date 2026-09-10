# Rack bring-up evidence

## Firmware and MCU

The supplied 524288-byte ROM is still scrambled: swap adjacent bits in each byte,
then XOR with `(17 * (offset + 1)) & 255`. Encoded SHA-256:
`14eb3479fa8815da967bf636915d46a9a125d137b357edb0e2eae3d34f6ebdb3`.
Decoded SHA-256:
`d9b199f27f573fdf7cd985fbd33cb9e60893a08912ab5d1bb562c77c663c997e`.
Both forms are accepted, other images rejected.

The synth application is file `0xc800..0x6a7df`, mapped to MCU `0x100000`.
File `0x800` is a maintenance utility, not the synth application. The current
emulator bypasses the maintenance/flash boot path and starts the synth image.

| Evidence | MCU address |
| --- | --- |
| Startup | `0x109754` |
| Upload routine / return | `0x10bad4` / `0x10977e` |
| Eight host pointers, spaced eight bytes | `0x15bd68` |
| Active DSP count byte | `0x1ab91c` |
| PIT tick counter / timed delay | `0x1c57e0` / `0x1009d2` |
| Recurring event loop / back edge | `0x100e6e` / `0x101218` |

Absent expansion hosts return HF2 low, selecting four DSPs. The apparent first-
second stall at `0x1009e6` is a normal timed delay; a five-second run executes over
37000 main-loop iterations. Unmapped MMIO and the default exception handler fail
explicitly. Panel ADC midpoint, released buttons, flash type and editor-port
wiring remain provisional; patch flash starts blank and is not persisted.

## DSP images and compiled helpers

`scripts/inspect_firmware.py` emits packed 24-bit DSP images, role-specific
disassemblies, focused MCU listings and an offset/hash manifest. Resident sources:
first `0x144644`, middle `0x144c18`, last `0x1451ec`. Each contributes `0x175`
words at P:0, followed by zeros through P:1ff and a five-word tail from `0x1457c0`
at P:200. All 517 resident words and the five initial X/Y tables are checked on all
four processors. Dynamic P:17 sample-vector and LA updates use the existing Micro
Modular fast-interrupt/JIT support.

`scripts/match_mcu.py` locates relocation-masked candidate helpers; candidates
were then disassembled and exercised, not blindly substituted.

| Operation | Micro helper | Rack helper |
| --- | --- | --- |
| Allocate module | `0x109b44` | `0x1106e0` |
| Set parameter | `0x109c4c` | `0x1107e8` |
| Connect cable | `0x1119a4` | `0x1192d6` |
| Volume target | `0x109706` | `0x11029e` |
| Compile orchestration | `0x1049c0` | `0x10b3da` |

The diagnostic patch is a common-area Oscillator A (type 7), output (type 5),
and one native cable. Slot A's active byte `0x1c3ab0` is consulted by `0x122438`.
The compiler returns success and uploads an actual oscillator graph to DSP0,
forwarding graphs to the middle DSPs, and the output mixer to DSP3.

## Bugs exposed by the Rack

1. ESSI mask reset state. The firmware leaves first/middle transmit masks and
   middle/last receive masks at reset. The generic emulator initialized these to
   zero. `nmrack` now initializes both halves of each mask to `0xffff`.
2. Missing DMA address mode. RX DMA2/3 use fixed peripheral sources and circular
   dual-counter destinations (DCR `0xa85140` / `0xa86140`, count 8, DOR2 -8).
   Previously this reached an unsupported-mode assertion; Release builds returned
   without copying data. The shared DMA implementation now handles fixed-source /
   dual-counter-destination transfers, including counter reload. Tests live here.
3. Peripheral service during sample interrupts. The sample graph is a long IRQ;
   DMA/ESSI must still advance at JIT boundaries. This is the same requirement as
   Micro Modular's native sample loop.

The slot-mask reset values and separate asynchronous RX/TX clocks are specified
in DSP56303UM sections 7.5.9–10 and 7.4.2. Its CRA prescaler definition gives
96 core cycles per 24-bit word for `0x181801`, and 144 for `0x181802` (PSR bypasses
the fixed /8). [NXP DSP56303 User's Manual](https://www.nxp.com/docs/en/reference-manual/DSP56303UM.pdf).

## Timing and remaining uncertainty

Machine time is expressed in nominal 96-kHz frames. Each DSP latches its time
origin when its loader completes; MCU and lagging DSPs receive bounded grants.
Host accesses catch up their DSP, and chain RX catches up its upstream producer.
Checked JIT linkage observes cycle/peripheral deadlines; sample deadlines keep
the inherited external IRQ approximation bounded. DMA/ESSI progress independently
of interrupt acceptance. The scheduling approach was informed by the reference
`gearmulator-md-mm-exp/source/elektron/md/mdLib/mdhardware.cpp`.

Experimental links queue timestamped two-word frames, reject overflow and prevent
future delivery. Disabled receivers discard traffic. Observed queue peaks are
14–15 packets; frame batching adds latency and is not pin-accurate. Both ESSIs
currently form forward chains; first RX timing, relative phases, GPIO gating,
external frame-sync edges and the final codec latch remain unresolved. In
particular, last-DSP transmit slot masking does not yet reconcile with its
two-word DMA blocks at the nominal sample rate. Do not hide this with resampling
or channel swaps.

The diagnostic renderer taps four final Y-memory mix values on sample-ISR return,
using provisional 18-bit DAC decoding. It is deliberately not claimed to be an
ESSI/codec-output renderer. A 96000-frame capture has a 330-Hz tone on zero-based
channel 3, AC RMS 0.036612555, with DC bias on the other channels. Intended output
alignment is wrong, which keeps the chain behind an explicit experimental flag.

Checked and unlinked JIT produced byte-identical one-second WAVs. The C++ hardware
regression also compares 9600 frames with request sizes 1, 7, 63, 64, 127, 128 and
255. Boot plus setup/warmup/capture took approximately 3.9 seconds linked and
4.4 seconds unlinked on this workspace; these are offline bring-up measurements,
not sustained real-time or polyphony benchmarks.

Regression caveat: the existing Micro Modular console's default minimal patch
produced silence with `--audio-driven 64 --cooperative --linked-jit`. Disabling
only the new Rack DMA branch and rebuilding produced a byte-identical silent WAV,
so this check also fails on the pre-Rack DMA behavior. The branch was restored.
This is not a passing Micro Modular audio regression; its peripheral tests pass,
and the pre-existing console/minimal-patch issue was left out of this task's scope.

Next investigation needs word-level RX/TX/FS observations, receiver DMA buffer
phase, and board GPIO/sample-clock gating. Correct channel alignment must precede
four-output/polyphony acceptance tests and user-facing plugin integration.

## Word-edge transport and startup gating (2026-09-09)

Implemented an opt-in `--word-serial` path without removing the frame-FIFO
reference. Shared ESSI hooks expose completed TX words before DMA refill and
accept externally clocked RX words before receive DMA. Internal RX ticks do not
duplicate externally supplied words. RX waits for a fresh frame sync after enable,
honors slot masks, and does not restart an unfinished frame on an early sync.
Callbacks and GPIO gates are unset for other emulator clients.

The worker scheduler now catches up upstream sources before independently
executing receivers. Direct propagation remains acyclic and exceptions escape
through the existing deferred peripheral-error boundary. Before this correction,
receivers could lead incoming words by 2.88 samples; afterward the maximum was
0.079 samples, attributable to bounded execution overshoot. This is not bit-exact
electrical timing.

The first word-mode capture contained only mixer DC. Boot observations identified
receive DMA advancing before firmware enabled ESSI0 pins: PCRC was zero while
PCRD was `0x3f`. The firmware enables receiver DMA immediately before setting
PCRC to `0x3f` at resident `P:0xcf`. Each ESSI is now gated while all six bits of
its own port control register are zero. This implements the all-GPIO inactive
condition described in [DSP56303 User’s Manual](https://www.nxp.com/docs/en/reference-manual/DSP56303UM.pdf),
not an inferred connection between MCU QS5 and an external clock. The word-level
frame abstraction follows section 7.4.7's completed-frame synchronization rule;
it does not resolve bit-frame timing, polarity or the physical board netlist.

With gating, `build/nmrack-word-gate.wav` contains a 330-Hz tone on diagnostic
channel 2 (zero-based 1), RMS `0.0366100616`, with zero AC on channels 1, 3 and 4.
The original paired-frame path instead put the tone on channel 4. In a 9600-sample
word trace, each inter-DSP port carried 86400 words. Nonzero oscillator words
arrived at `X:0x6c1` on all three ESSI0 links (9600 occurrences each); all six
receive DMA streams had zero address discontinuities. See
`build/nmrack-word-gate-summary.json`, generated by `scripts/check_words.py`.
No channel swapping or RX address patch was used.

New bounded diagnostics preserve MCU QS transitions and DSP port/RX-DMA control
changes from boot, plus per-word snapshots during the requested capture. DSP
control-change timestamps are block-boundary observations, not exact writes.
QS transitions observed were `04 -> 00 -> 04 -> 24`, the last at machine frame
1662.91. Their physical clock routing remains unknown and is not hardwired.

Tests exercise TX-before-refill, RX-before-DMA, nine-word wrapping across frame
boundaries, disabled and masked slots, frame gaps/re-enable, clock gates, and
budget errors in both transport modes. Hardware tests require diagnostic channel
2 tone amplitude/frequency and isolation, compare linked/unlinked JIT, and compare
fixed 7/128 with irregular output targets. The legacy mixer tone test remains.

Remaining acceptance work: acquire/reconstruct the final DSP's physical DAC
latch/frame timing, validate four distinct output routes, then general patches,
MIDI/polyphony and callback-isolated standalone/plugin integration. A correctly
aligned memory-tapped tone is progress, not a validated codec or playable plugin.

## DAC transaction reconstruction and strict output acceptance

`DacOutput` now consumes newly loaded final-DSP ESSI TX words, not a memory tap.
In the callback, TDE still describes the outgoing register: a clear TDE identifies
a new payload before the next DMA refill. DMA4/5 use postincrementing Y sources,
so `DSR-1` supplies provenance for the outgoing word. The assembler checks source
pair, alternating `6c0/6e0` banks, duplicates, completeness, skew and frame cadence.
Identical-valued new samples remain distinct; held-register repeats are excluded
by status, never by comparing their numerical values. Optional validation compares
the outgoing raw payload against Y at its DMA source; audio never falls back to Y.

This is explicitly a **firmware transaction reconstruction**, not a pin-accurate
DAC. Freshness and source addresses are emulator metadata, unavailable to a real
DAC. The current final-ESSI model still has three active transmissions per sample
per port, only two freshly loaded. A one-second capture had 384000 fresh words,
192000 held words, 96000 completed frames and maximum pair skew 0.502315 samples.
Identifying physical latch/frame selection remains necessary before claiming
hardware-equivalent DAC operation.

The native type-3 four-output module was used for a new acceptance fixture. Four
type-7 oscillators use pitch parameters 57/61/65/69 (nominal 220/277.18/349.23/440
Hz), fine pitch 64, and output level 100. In the generated first-DSP program,
oscillators write X:e/10/12/14 at P:286/375/464/553. The output module at P:555..562
reads those signals in order X:10/e/14/12 and writes four consecutive mix words.
Thus the transaction DAC reverses each pair to expose logical input/output order.
The [Nord module reference](https://www.nordkeyboards.com/wt/documents/235/Nord%20Modular%20English%20User%20Manual%20v3.0%20Edition%203.0.pdf)
describes the four-output module as routing its four inputs to their respective
mix buses/output jacks. This supports the intended logical routing, not a measured
analog pinout.

### Initial acceptance failure (resolved below on 2026-09-10)

| Logical output | Expected tone | Observed serial reconstruction |
| --- | --- | --- |
| 1 | 220 Hz | 440 Hz |
| 2 | 277.18 Hz | 220 Hz |
| 3 | 349.23 Hz | DC only |
| 4 | 440 Hz | 349.23 Hz |

`check_words.py` on `build/nmrack-dac-four-words.csv` locates all four first-DSP
tone words at downstream receive addresses `6c8,6c0,6c1,6c2`, with no address
discontinuities. The same offset persists through all three links. The missing
277-Hz tone is therefore already outside the final mixer's four output locations;
this is not a signed-DAC conversion problem. The earlier one-tone output-2 result
was insufficient to certify routing and is not treated as such.

`nmrackDacTests` is registered as an ordinary CTest test and initially returned failure on
this defect (no WILL_FAIL exemption). Its payload, bounded-target, cadence,
linked/unlinked and fixed-7/128 versus irregular buffer comparisons pass before
the spectral/channel checks fail. The existing three regression suites still
passed. This initially left the test suite red on the unfulfilled four-output
acceptance criterion rather than weakening the test or introducing a compensating
channel shuffle. The investigation below resolves that failure without changing
the output mapping or rewriting DMA buffer addresses.

### Real-time constraints

`Playback` runs the machine exclusively on one producer thread. A lock-free SPSC
queue decouples 128-frame worker renders from the callback, with a 512-frame target
at 96 kHz. The callback performs bounded copies and zero-fill only. It reports
shortages via lock-free counters; exceptions, logging, allocation, firmware/JIT
execution, waits and joins stay off that callback. Board-state tracing is now
explicitly opt-in instead of sampled on every worker block by default. External
advance calls are rejected while an audio target is active to prevent undrained
queue overflow. Worker shutdown/error propagation and zero-fill underruns are
implemented; control changes and sample-rate conversion still need host adapters.

The paced three-second single-tone and ten-second four-oscillator CLI callback
simulations completed with zero underruns and zero missing frames (the latter
with DMA payload validation enabled, logged in `build/nmrack-dac-realtime10.log`).
Unpaced four-oscillator worker captures exceeded real-time
speed locally (about 1.4x in one validation-enabled run). These are smoke/throughput
measurements, not an OS audio-device test or a sustained real-time guarantee.

## 2026-09-10: fix timing-critical JIT execution boundaries

The receive-slot rotation was sensitive to JIT execution granularity. A complete
one-instruction-block reference run placed all four tones correctly, unlike the
16-instruction baseline. It required approximately 793 million execution steps
and 9.35 wall-clock seconds for the CLI boot/setup/capture, so globally reducing
block size was rejected as the production solution. The reference's larger step
budget was an investigation-only CLI setting; the normal test budget is unchanged.

Controlled comparisons isolated two required regions in the verified firmware:

| Precise region | Four-output result |
| --- | --- |
| None (16-instruction baseline) | Shifted outputs, missing tone |
| Initialization/resume only, P:9e..169 | Still fails |
| Sample prefix only, P:175..19c | Still fails, with different rotation |
| Both regions | All four tones pass |
| Entire program (reference) | All four tones pass, excessive overhead |

These regions change DMA enables/source banks, port controls and interrupt masks.
Deferring peripheral observation across a larger native block can change which
serial edge sees those changes. `jitconfig.h` selects single-instruction blocks
there using the core's existing per-address configuration facility. Normal idle
and arithmetic blocks remain bounded at 16 instructions. Precise blocks may link
only with peripheral/owner deadline checks, retaining real-time headroom. This is
Rack word-mode policy; the legacy frame path and shared DSP/MCU implementations
were not changed for this fix.

An alternate sample-clock experiment using the first ADC frame callback failed
the DAC cadence check and was removed. IRQD still uses the explicitly provisional
96-kHz board model. Neither clock phase offsets, output permutations, firmware
patches nor RX destination rewrites were used to make acceptance pass.

`nmrackDacTests` now additionally delays patch loading by 1 and 17 machine samples
and repeats the spectral/isolation checks. All channels pass at nominal
220/277.18/349.23/440 Hz. The original linked/unlinked and fixed-7/128 versus
irregular target comparisons remain bit-exact. Unit tests enforce the precise
address boundaries and deadline-checked link policy.

Local validation-enabled worker captures measured approximately 1.29–1.31x
real-time speed with checked links (1.05x in the unlinked reference run). A fresh
ten-second, four-oscillator paced callback simulation completed with zero
underruns and zero missing frames; see `build/nmrack-phase-realtime10.log`.
This fixes digital transaction output acceptance, not the remaining unmeasured
physical DAC latch timing, general polyphony or host audio-device integration.

Final verification: all four registered regression suites passed (44.12 seconds).
The fresh `build/nmrack-aligned-words.csv` capture places the four tones at
`6c0..6c3` through the chain, without receive-address discontinuities. The matching
`build/nmrack-aligned.wav` contains 96000 DAC frames checked against 384000 fresh
TX payloads. Raw captures remain build artifacts; no firmware or signal buffers
were patched to obtain these results.

## Native patch and MIDI worker (2026-09-10)

The standalone player now loads a parsed patch on its emulation worker before
prefill. This reuses the Micro Modular **parser**, not its hardware implementation
or MCU addresses. Rack native compiler helpers generate all DSP programs. Patch
load remains initialization-only; complete editor SysEx and gapless replacement
are not implemented. Morph mappings are rejected rather than silently discarded.

Additional helper mapping, verified by relocation-masked matching followed by
Rack disassembly and runtime tests:

| Operation | Micro Modular | Rack |
|---|---|---|
| Custom-data pointer / size | `109df8` / `109e72` | `110994` / `110a0e` |
| Controller mapping | `10a060` | `110bfc` |
| Knob mapping | `10f2c6` | `1160a0` |
| Requested voices | `10a278` | `110e14` |
| Note / velocity ranges | `10c308` / `10c35c` | `112ea4` / `112ef8` |
| Bend range | `10c2be` | `112e5a` |
| Portamento / time | `10c1ee` / `10c236` | `112d8a` / `112dd2` |
| Octave / area separator | `1011ae` / `10c384` | `101e0e` / `112f20` |
| Voice/common retrigger | `10c1c8` | `112d64` |
| Live coefficient broadcast | `106d40` | `10d8d8` |

The Rack uses a 32-bit slot stride calculation where several Micro helpers use
16-bit operations, so byte matching alone misses valid correspondences. The
Rack slot base is `1ab988`, stride `6000`; common/poly area bases retain offsets
`467a` / `4c66`. Note and velocity limits retain `5a1b..5a1e`; leaving the upper
velocity limit at reset zero prevents MIDI notes. Requested voices is `5a1a`.
The native compiler performs resource allocation and may allocate fewer voices
than requested. `FourVoices.pch` with request 32 allocates **28**, seven per DSP,
with distinct `(DSP index, sample-program address)` bindings. Tests hold all 28
notes simultaneously and check active bindings on all four DSPs. Default request
four allocates four programs on DSP 0, not one voice per DSP.

Bindings retain 14-byte records at poly+`228`, DSP index byte +0 and sample P
base word +6. MCU voice records at poly+`3e8` retain note/velocity/state at
+9/+10/+12. Tests verify note sets and velocity, not just increased audio RMS.
The four-DSP chord has AC RMS about 0.102 versus about 0.018 for one note in that
allocation. DC bias is removed per channel for silence assertions, not by changing
the emulated output. Pitch bend produces 29 rising crossings per 100 ms, consistent
with the native two-semitone C4-to-D4 bend. Parameter/CC tests allow native level
smoothing to settle before asserting final gain.

MIDI enters `QSM::writeSciRX` on the worker with cycle-based receiver timing.
No native MIDI dispatcher is called directly. The 256-entry timestamp-sorted
control inbox supports multiple producers, uses try-lock on the worker, and is
never touched by the audio callback. Rendering splits at native-frame timestamps;
MIDI delivery is additionally spaced at 31 native frames per byte (slightly
conservative versus 30.72 at 31.25 kbaud). Same-time chords therefore serialize.
Late counters exclude this deliberate wire-rate delay. Parameter calls execute
only at MCU `100e6e` with a bounded safe-point wait, then run `1107e8` storage and
`10d8d8` broadcast, retaining audio generated during native calls.

The initial patch job runs before audio callbacks start. Runtime parameter/MIDI
commands are fixed-size; file I/O, parsing, native execution and JIT never run on
the audio callback. Producer overflow is explicit and requests a separate panic:
discard pending commands, release sustain and send all-notes-off on all channels.
This is bounded recovery, not lossless operation under unbounded event traffic.

Native MIDI onset measured 137–139 frames (1.43–1.45 ms). The 48-kHz worker test
observed onset at frame 9665 for scheduled frame 9600, with zero underruns or late
commands. This measures the scheduled output timeline, not physical MIDI-to-jack
latency; producer lead, device buffers and real MIDI delivery add latency.
Seven regression suites now cover unit/peripheral, hardware, DAC, single-DSP
MIDI, all-four-DSP MIDI and paced worker control behavior. Local logs are in
`build/nmrack-midi-controls-tests.log` and `build/nmrack-midi-regressions.log`;
the former contains the final passing MIDI/control rerun after extending the
controller smoothing observation window.

The full-board real-device stress case (`--voices 28 --midi-demo`, muted MacBook
Pro Speakers, 48 kHz/256, 180 seconds) **failed real-time acceptance** despite
functional native polyphony: 8638976 device frames, 508928 missing frames,
3976 deficient chunks, worst 128-host-frame render 9.7935 ms, average process CPU
0.9997 cores. Backend xruns and command rejection/lateness were all zero. See
`build/nmrack-midi-device180.log`. Approximately 94.1% of requested frames were
available, so this is a sustained producer throughput problem, not a reason to
hide underruns with a larger queue. No timing precision was relaxed to mask it.
Firmware allocation of 28 voices must not be presented as 28-voice real-time
support. Larger voice counts need additional JIT/worker performance work.

The default four-voice 180-second test with Volt 476 MIDI input open used 0.7967
CPU cores but missed one 128-frame chunk (backend xruns and late/rejected commands
zero). Worst render was 9.458 ms. This had average throughput headroom unlike the
28-voice overload. The player queue target was increased from three to four device
blocks (16 to 21.33 ms at 48 kHz/256) to add scheduling margin; the firmware clock
and JIT precision were unchanged. The original result remains recorded in
`build/nmrack-midi-four-device180.log`, not hidden by the queue adjustment.

Final four-block retest: **PASS**, 180.021 seconds, 8639232 host frames, zero
underruns/missing frames, backend xruns, late commands or rejected commands.
Average process CPU was 0.793291 cores; worst worker render 9.85742 ms. The test
used four native voices, muted MacBook Pro Speakers output at 48 kHz/256, and
Volt 476 MIDI input open. MIDI notes were generated by the built-in demo, not an
external keyboard. Log: `build/nmrack-midi-four-buffered180.log`. This establishes
a sustained default-patch playback case; the full-board 28-voice throughput
failure above remains a separate unresolved optimization target.
