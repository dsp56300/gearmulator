# Nord Micro Modular plugin and standalone

The `nmmPanel` RmlUi skin uses the supplied `nordmm-final.jpg` unchanged as its
faceplate. Four live rotary controls (Volume plus assignable 1–3), three
buttons and a two-digit LED overlay occupy the photographed control positions.
Coordinates are calibrated to the cleaned image’s 1571 × 922 canvas; Patch Select
shares knob 3’s bounds so toggling Shift does not move the control.
Default editor size is approximately 1335 × 783 at the skin's 85% root scale.
VST3 and standalone instantiate the same processor/editor/skin.

The Nord Lead/NodalRed2x skin supplies `button_black.png`,
`knob_big_page0.png` and `DSEG7Classic-BoldItalic.ttf` directly. They are bundled
and included in skin exports without maintaining duplicate copies. Its
128-frame knob sprite declarations are reused in the RCSS. The shared
`jucePluginEditorLib::Processor`, `PluginEditorState`, `Editor`, parameter
bindings and `createJucePlugin` CMake function provide the normal framework
behavior, including **Escape to open/close settings**, GUI scale, renderer,
MIDI routing/learn, audio/resampling and latency settings. N2x-specific MIDI
protocol classes and its three-character scrolling display are not suitable
for the Micro's firmware and two-digit panel.

## Build and run

Use the root README's dependencies and the parent NMM README's build options.
Enable JUCE and standalone, then build the standard product targets:

```sh
cmake -S . -B build/nmm-ui -DCMAKE_BUILD_TYPE=Release \
  -Dgearmulator_BUILD_JUCEPLUGIN=ON \
  -Dgearmulator_BUILD_JUCEPLUGIN_VST3=ON \
  -Dgearmulator_BUILD_JUCEPLUGIN_Standalone=ON \
  -Dgearmulator_SYNTH_NORDMICROMODULAR=ON
cmake --build build/nmm-ui --target nmmJucePlugin_VST3 nmmJucePlugin_Standalone
```

Disable the other synth/format options when only building this product. On the
local Command Line Tools / arm64 setup the configuration also used
`-DCMAKE_OSX_ARCHITECTURES=arm64 -DXCODE_VERSION=99` (the existing repository
configuration workaround).

The standard framework places Release products in:

- `bin/plugins/Release/VST3/NordMicroModular.vst3`
- `bin/plugins/Release/Standalone/NordMicroModular.app` on macOS

The framework's firmware folder is `Documents/The Usual Suspects/NordMicroModular/roms`.
Supply the same decoded OS 3.03b image accepted by the console; candidates are
checked by exact size and SHA-256. Firmware is not embedded. The UI still opens
with the framework's missing-firmware message when no valid image is found.

## Controls and current scope

- Volume drives the original MCU master-volume setter and DSP coefficient ramp.
  The AD1865 output is decoded as signed 18-bit audio. The former 512× monitoring
  gain and hard output clamp have been removed. A unity-gain 10 Hz DC blocker
  remains as an approximate output-coupling model.
- Knobs 1–3 use module assignments 0–2 in `[KnobMapDump]`. Module parameters
  are changed through the existing bounded firmware-call adapter on the
  emulator worker. Unassigned knobs say so in their tooltip. Morph assignments use the native morph conversion and broadcast path.
- The upper-right panel button increments the selected patch and the lower-right
  panel button decrements it, skipping empty bank positions and wrapping at the
  ends. Toggle Shift on before pressing the upper button to send channel-1 note
  60 / velocity 100; with Shift on, the lower button holds the patch's assigned
  Button 4 parameter at its pressed value. Turning Shift off, closing/hiding the editor,
  pressing Escape, or opening the patch chooser
  releases momentary functions. Host/physical MIDI follows the framework
  routing path.
- Toggle Shift on while turning knob 3 to select patches. With Shift off,
  it continues to control its assigned module parameter. The selector uses the
  same Nord Lead knob component with integer steps and is available even when
  the current patch has no knob-3 assignment. Turn Shift off to return to the
  assignment. No audio-thread compilation or filesystem access is added.
- The initial bank is `01: 101`, `02: SimpleSqr1`, `03: BasicOsc`, `04: FourVoices`. The shared
  Options menu's `Load Patch...` entry opens a `.pch` chooser and adds the imported patch to the bank (identical content
  reuses its entry). Empty positions are reused first. Browser patch lists show
  the same bank; native browser saves populate their numbered slots, and the knob
  skips empty positions in both directions. Up to 99 positions are available; importing when full replaces
  the selected entry. File parsing, boot and compilation occur on the worker.
  The LED shows the selected bank position immediately, flashes during loading,
  and shows `Er` on failure; the status text identifies the patch or error.
- The remaining Shift+Knob 1/2 functions (Master Tune and MIDI Channel) are
  still pending; the panel buttons and Shift+Knob 3 patch selector follow the
  hardware behavior above.
- Native state v4 contains the bank, selected position, panel values, 1 MiB flash and native
  patch packets, preserving browser edits, module layout/names and mappings.
  Older v1/v2/v3 states remain readable. Switching bank entries preserves edits.
  Device state appends to the shared framework's version/type header. It is not
  a cycle-exact CPU/DSP savestate.
- Remote DSP bridge is explicitly unsupported for this device. Local operation,
  shared settings, host MIDI and optional stereo input are implemented. Open
  `StereoInput.pch` and enable the host input bus to test the input path.
  ADC filtering and physical converter-clock accuracy still need validation.

## Nord Modular web editor

The plugin and standalone create instance-specific virtual **PC In / PC Out**
ports on macOS and Linux. Press **Escape → MIDI** to enable/disable them and see
the instance name. In [the web editor](https://nordmodulareditor.com/g1-patch-editor),
allow MIDI/SysEx, select **PC Out as input** and **PC In as output**, and use slot A.
Use ordinary MIDI or Note Trig to play; audio comes from the native emulator.
Discovery, patch reads, live parameter changes and patch uploads run through the
original MCU firmware's emulated PC UART. The editor also works while the audio
host is stopped. Knob mappings/values follow native editor changes.

**Chrome 152.0.7977.77 on this Mac required `--disable-features=MidiMacUmp`** to
receive SysEx replies. See the [connection instructions and investigation](../../../../nord-micro-modular/analysis/editor-midi.md)
for the tested launch command, protocol details and limitations. Linux remains
untested; Windows virtual PC ports are not implemented. The plugin bank and host
state preserve edits and raw flash contents. Native flash storage and the shared
browser/panel list/load/delete workflow are integrated. Complete global-settings
workflows still need validation.

## Audio-thread boundary

The experimental `Hardware::render()` still allocates and can compile JIT code.
The device adapter uses caller-owned `renderInto` buffers and runs firmware boot,
patch parsing, parameter calls and
cache rebuilding on one dedicated worker. No GUI callback accesses a running
CPU/DSP directly. The device audio callback uses fixed SPSC job/output rings,
fixed MIDI arrays and atomics, without emulation, filesystem work, locks,
allocation or waits in realtime mode. Equal-offset MIDI preserves source order.

The worker delay is **at least 1024 native frames / 10.67 ms** at 96 kHz.
It grows for large host blocks: `max(1024, ceil(nativeBlock / 256) * 256 + 256)`.
The framework supplies the host block size converted to the device rate; other
synths keep their existing behavior. Input/output adds the 256-frame input queue.
Resampling, audio-interface buffering and the framework's additional latency
setting add to these figures. Additional latency now delays actual Nord output,
as well as appearing in the host's reported latency.

Late output becomes silence; queue overflow discards obsolete MIDI batches and
requests all-notes-off so dropped note-offs do not leave voices stuck. Counters
are retained in `PanelState`. Loading produces intentional silence and does not
queue playback jobs or MIDI for the next patch. Retrigger notes after loading.
Generation checks discard superseded jobs and output. Host offline rendering
explicitly waits for completed jobs via JUCE's `setNonRealtime` hook; realtime
never waits.

Patch selection coalesces rapid changes for 25 ms and cancels superseded loads.
Previously visited patches restore native editor packets into the existing
MCU/DSP, retaining the firmware's compilation path. First-time text imports and
host-state bank replacement still boot a fresh machine; the validated ROM is
shared within the device so these loads do not repeat file reads and hashing.
Stopped hosts suspend emulation when editor UART work and controls are idle;
2 ms polling keeps editor discovery available. Active playback polls at 500 us.

This boundary does not certify every shared-framework callback as allocation
free. Existing resampling/MIDI code, sustained load, multiple instances, extreme
MIDI traffic, patch changes, and final codec timing still need product testing.

## Verification

`nmmTests` covers the patch parser, including area identity, requested polyphony, morphs and knob mappings. The optional
firmware-dependent worker test renders a MIDI note and release in 128-frame
blocks, checks queue continuity, then restores patch/knob state:

```sh
build/nmm-ui/source/claudia/nmm/nmmJucePlugin/nmmDeviceTests \
  '/path/to/decoded firmware.bin' nord-micro-modular/patches/101.pch
```

A paced five-second four-voice check disables offline waiting and requires
zero worker underruns/dropped jobs while native editor parameter changes run:

```sh
build/nmm-ui/source/claudia/nmm/nmmJucePlugin/nmmDeviceTests \
  '/path/to/decoded firmware.bin' nord-micro-modular/patches/FourVoices.pch --realtime-four
```

`nmmUiSmoke` is a development GUI check, not the shipped standalone. It accepts a
firmware directory and screenshot prefix, verifies the panel elements, and opens
and closes the real shared settings using Escape events, including a rapid
reopen/close before deferred callbacks run. This exposed a shared settings
callback lifetime bug; deferred callbacks now check a lifetime token before
accessing a closed settings panel. The test saves panel/settings screenshots
using the software renderer and tears down the plugin. Run it with normal
desktop access.

The UI check toggles Shift on and presses the upper panel button through RmlUi
mouse hit testing, then renders the real processor at 48 kHz with an empty host MIDI
buffer. It checks sustained audio and release after mouse-up, Escape, and host
editor closure.
This exercises the shared MIDI routing/resampler and emulator worker together.
It also toggles Shift on, turns Patch Select using mouse-wheel events, checks the
LED at each step and round-trips the bank/selected position through the shared
plugin state API before rendering notes again.

The panel button and Patch Select behavior follows pages 12, 18 and 68–69 of the
[Nord Modular v3 manual](https://www.nordkeyboards.com/wt/documents/235/Nord%20Modular%20English%20User%20Manual%20v3.0%20Edition%203.0.pdf).

`nmmUiSmoke firmware-directory output-prefix --realtime` additionally tests
the default hardware audio output using JUCE's AudioDeviceManager and
AudioProcessorPlayer, with offline mode disabled. `nmmStandaloneSmoke` uses
the exact JUCE StandaloneFilterWindow/StandalonePluginHolder used by the shipped
app, including output-device initialization and state loading. Its optional
argument is the standalone `.settings` file, loaded into a disposable in-memory
copy. It toggles Shift on before pressing/releasing the upper panel button and checks
an audible output level. Both live tests play one note through the selected
output.
Add `--recover` after the settings filename to test using Shift/Patch Select to
recover to bank 01 from an unsupported saved patch (`Er`), retaining that patch
in the bank. Invalid files remain explicit errors; selection does not silently
replace their data.

On macOS, the standalone's **Options → Audio/MIDI Settings** selects the physical
audio interface and output channels. Escape opens the shared plugin settings.
The local live tests used the default Volt 476 output at 44.1 kHz / 512 frames.


The [runtime implementation notes](../../../../nord-micro-modular/analysis/realtime-runtime-implementation.md)
include the paced multi-instance stress command and remaining hardware-fidelity
limits. Read-only native snapshots no longer advance the audio timeline. Internal
input latency is at least 1280 samples at 96 kHz, including the 256-sample input
staging queue; MIDI-to-output latency is at least 1024 samples before framework
additions. See [latency/loading measurements](../../../../nord-micro-modular/analysis/latency-loading.md)
for buffer sizing, load regressions and CPU measurements.

## Extended host stress and CPU profiling

Build `nmmHostStressTests` and `nmmRealtimeStressTests`, then run from the repository root:

```sh
python3 source/claudia/nmm/scripts/stress-runtime.py \
  --firmware /path/to/firmware.bin --output /tmp/nmm-stress-results
```

The default runs five minutes of mixed shared-host processing and five minutes
of steady two-instance playback, sequentially. The mixed run covers eight
patches, three sample rates, four block sizes, extra latency, native flash saves,
automation, state restore and callback/transport stop-resume. It records complete
logs and JSON results. This simulates the shared host framework; it does not
replace DAW or JUCE GUI integration tests.

The companion `scripts/profile-runtime.py` captures macOS stacks separately. See the
[CPU profile and extended stress notes](../../../../nord-micro-modular/analysis/steady-state-cpu.md)
for measurements, reproducible commands and the evaluated optimization candidate.

For generated DSP block attribution, build `nmmJitProfile` and use
`scripts/profile-jit.py`. The fixed-patch capture checks before/after native
address maps and reports unresolved helpers separately. `nmmHostStressTests`
also accepts `--callback-cpu` as its last argument to record thread CPU time
alongside long callback wall times. These diagnostics are opt-in and do not
add instrumentation to normal plugin playback. Tail records are printed even
when the stress test fails; queue failures include phase, format and counters.

The extended runner now saves host state concurrently with playback in every
patch segment. Add `--callback-cpu` to retain callback wall/thread CPU tails,
worker job durations and the first missed-frame queue position. Active playback
uses the repository's High worker priority; loading, offline rendering and
stopped-host work use normal priority. Unsupported priority elevation is
reported by the existing platform helper and playback continues.

Read-only state snapshots wait for incoming editor edits to settle independently
of outgoing meter notifications. Full UART-idle detection still governs stopped
worker suspension and native command ordering.

For a real audio-device test, `nmmStandaloneSmoke` accepts `--block=64`,
`--four-voices` and `--hold-seconds=30` after the settings-file argument. This
uses a temporary settings copy, requests 44.1 kHz, checks the actual accepted
buffer size, holds a four-note chord and fails on any device queue loss.

Short-release chord diagnostics:

```sh
nmmChordTests firmware.bin FourVoices.pch --realtime
nmmMidiDeliveryTests
```

The chord test repeatedly retriggers all four pitches through the plugin worker
and resampler at 44.1 kHz/64 samples, sets release to zero, checks each fundamental
independently and rejects audio/job loss. Omit `--realtime` for offline execution.
`nmmHardwareTests` also checks 128 short-gap chords across reference and burst DSP
schedulers, comparing MCU note ownership and DSP audio. MIDI delivery tests check
ordering and retention across host rates and buffer sizes. These tests currently
pass; they do not yet reproduce the reported intermittent missing voices during
manual FourVoices playing.


### Default audio scheduler

Plugin and standalone playback now use fixed 64-frame audio batches on the
existing hardware worker, with checked JIT linking. No extra DSP thread is
started. Bounded capture remains opt-in and uses the native batching path.
See [promotion validation](../../../../nord-micro-modular/analysis/default-audio-batching.md)
for default-mode latency, loading, voice and host regression coverage.
