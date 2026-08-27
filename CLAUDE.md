# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Gearmulator is a low-level IC emulator that recreates classic virtual analog synthesizers (Access Virus, Waldorf microQ/XT, Clavia Nord Lead 2x, Roland JP-8000) by emulating original DSP56300, MC68K and H8S processors and running authentic firmware ROMs as audio plugins (FST, VST3, AU, CLAP, LV2).

The project emphasises **accuracy of emulation over shortcuts** — the goal is to run original firmware bit-identically to hardware.

## Build Commands

**Current dev setup uses `temp/cmake_vs26` with Visual Studio 2026.**

```bash
# Configure (Windows)
cmake . -B temp/cmake_vs26 -G "Visual Studio 17 2022"

# Build (use Debug for quick compile checks, Release for full optimization)
cmake --build temp/cmake_vs26 --config Debug -j 4
cmake --build temp/cmake_vs26 --config Release -j 4

# Package
cd temp/cmake_vs26 && cpack -G ZIP        # or -G DEB / -G RPM on Linux

# Run tests
ctest -C Release
```

macOS uses the Xcode generator: `cmake -G Xcode -S . -B temp/cmake`.

Per-synth CMake flags: `-Dgearmulator_SYNTH_OSIRUS=ON`, `_OSTIRUS`, `_VAVRA`, `_XENIA`, `_NODALRED2X`, `_JE8086`. Plugin format flags: `gearmulator_BUILD_JUCEPLUGIN`, `_VST2`, `_VST3`, `_CLAP`, `_LV2`, `_AU`, `_Standalone`, plus `gearmulator_BUILD_FX_PLUGIN`.

Convenience scripts: `build_win64.bat`, `build_linux.sh`, `build_mac.sh`.

**Test consoles** — the fastest way to iterate on DSP/device code without building a plugin:
`virusTestConsole`, `virusIntegrationTest`, `mqTestConsole`, `xtTestConsole`, `n2xTestConsole`, `jeTestConsole`.

### Platform-specific build settings

Set in `source/cmake/base.cmake`:
- **Windows**: MSVC Release uses `/O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot`
- **Linux**: `-Ofast -fno-stack-protector`, LTO enabled (except GCC, due to compiler bugs)
- **macOS**: universal binaries (x86_64 + arm64), minimum 10.12/10.13, `-Ofast -flto`
- **ARM**: special handling for ARMv8.1a+ atomics on known boards (rk3588, rock-5b, rpi-2712)
- C++17 required — the DSP emulator uses extensive compile-time metaprogramming

## Source Tree Layout

`source/` is grouped by ownership, then by manufacturer. Manufacturer folders use
codenames to stay clear of trademarks (`ronaldo` = Roland, and so on).

```
source/
├── cmake/        build glue: juce.cmake, skins.cmake, base.cmake, xcodeversion.cmake, …
├── 3rdparty/     everything not written by us — see source/3rdparty/README.md
├── cpu/          processor cores: dsp56300, mc68k, h8s
├── framework/    synth-agnostic code
│   ├── baseLib/ synthLib/ hardwareLib/ networkLib/
│   ├── juce/     jucePluginLib, jucePluginEditorLib, juceRmlUi, juceRmlPlugin,
│   │             juceUiLib, jucePluginData, mcpServerLib
│   └── tools/    bridge, pluginTester, midiLearnTest, changelogGenerator
├── axel/         Access
├── waldi/        Waldorf — common/, microq/, xt/
├── claudia/      Clavia
└── ronaldo/      Roland — common/, esp/, je8086/
```

The rule for placing something new: `<maker>/<family>/<target-dir>`, plus
`<maker>/common/` for code shared across that maker's families and `<maker>/<chip>/`
for that maker's own custom silicon. A processor core used by more than one
manufacturer goes in `cpu/`. A maker with only one family skips the family level.

**Include paths do not encode this layout.** Every library exports its own parent as
its include root (`target_include_directories(<lib> PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..`),
so you still write `#include "synthLib/device.h"` regardless of how deeply nested
either side is. Keep that line when adding a library, and never write depth-relative
includes like `../../foo/bar.h` — they break the next time anything moves.

## Architecture

**Emulation stack:** DSP56300 emulator (`source/cpu/dsp56300/`, JIT via asmjit; peripherals ESAI and HDI08) + MC68K emulator (`source/cpu/mc68k/`, Musashi) + H8S core (`source/cpu/h8s/`). Each synth has a device library that loads firmware ROMs, initializes processor memory, and handles MIDI/audio via HDI08.

**Per-synth pattern:**
| Emulator | Hardware | Device Lib | Plugin Dir |
|---|---|---|---|
| Osirus | Virus A/B/C | `axel/virusLib/` | `axel/osirusJucePlugin/` |
| OsTIrus | Virus TI/TI2/Snow | `axel/virusLib/` | `axel/osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `waldi/microq/mqLib/` | `waldi/microq/mqJucePlugin/` |
| Xenia | Waldorf MW II/XT | `waldi/xt/xtLib/` | `waldi/xt/xtJucePlugin/` |
| Nodal Red 2x | Nord Lead/Rack 2x | `claudia/n2x/n2xLib/` | `claudia/n2x/n2xJucePlugin/` |
| JE-8086 | Roland JP-8000 | `ronaldo/je8086/jeLib/` | `ronaldo/je8086/jeJucePlugin/` |
| DSPBridge | Network bridge | `framework/tools/bridge/` | — |

Shared per-manufacturer code: `waldi/common/wLib/` (microQ + XT), `ronaldo/common/` and `ronaldo/esp/` (Roland).

**Shared libraries** (all under `source/framework/`):
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `juce/jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `juce/jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juce/juceRmlUi/` — RmlUi integration (HTML/CSS-like UI framework)
- `juce/juceUiLib/` — Common UI components
- `juce/jucePluginData/` — Shared plugin assets and RML/RCSS templates
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions
- `networkLib/` + `tools/bridge/` — optional network bridge for remote hardware

**Plugin build flow:** `createJucePluginWithFX()` macro in `source/cmake/juce.cmake` → links device lib → Processor inherits `synthLib::Plugin` wrapping `synthLib::Device` → skins via RML/RCSS compiled into binary data, multiple skins per synth via `addSkin()` / `buildSkinHeader()`.

**Device model pattern** — every synth follows it:

```cpp
namespace <synthName>Lib {
    class Device : public synthLib::Device {
        // DSP/MC68K instance, microcontroller for MIDI/UI,
        // ROM loading and memory setup, audio I/O via HDI08 or similar
    };
}
```

Responsibilities: load firmware ROM, initialize DSP/CPU memory, handle MIDI via microcontroller or direct DSP communication, process audio blocks, keep front panel state in sync.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`virusLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- **Match the file you are in** — respect existing local patterns over these defaults
- `.clang-format` in `source/` directory
- C++17 required

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval — the user stages changes themselves
- Git remotes: `gearmulator` (public OSS, `dsp56300/gearmulator`), `private` (development), also `nas`, `codeberg`, `EvilDragon`. Jenkins SCM points at `private`.
- DSP submodule (`source/cpu/dsp56300/`) is also owned by user — changes there are fine
- The device branches are **worktrees of this repository**, so they share one `.git` and one set of remote-tracking refs. A push from any session moves the shared ref: check `git ls-remote` rather than assuming something is unpushed.
- `oss/main` is the public remote. Unreleased devices must not be named in anything that lands there; private branches need no such care. See §9 of `doc/restructure_plan.md`.

## Key Build Files

- `source/cmake/base.cmake` — Compiler flags, platform-specific optimization settings
- `source/cmake/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/cmake/skins.cmake` — Skin asset compilation
- `source/cmake/exporttarget.cmake` / `changelog.cmake` — target export, changelog generation
- `scripts/pack.cmake` / `generate.cmake` / `deployAll.cmake` / `deployGitHub.cmake` — CI packaging and deploy
- `source/3rdparty/README.md` — which third-party code is forked, why, and how to update it
- `scripts/Jenkinsfile` / `JenkinsfileMulti` — Private CI (Jenkins)
- `.github/workflows/cmake.yml` — Public CI (GitHub Actions)

## Where to Make Changes

1. **Device-level changes** → the device lib (e.g., `source/axel/virusLib/device.cpp`)
2. **Plugin UI** → the plugin dir (RML/RCSS for layout, processor for logic)
3. **Shared plugin infra** → `source/framework/juce/jucePluginLib/` or `source/framework/synthLib/`
4. **DSP emulator** → `source/cpu/dsp56300/source/dsp56kEmu/`
5. **New parameter** → update `parameterDescriptions_*.json` in the plugin dir, map to MIDI CC/SysEx in the processor, update skin RML if it should appear in the UI

Test with the test consoles before building full plugins.

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` with `insert()`, never `assign()` — see the State Save/Restore section below
- **RmlUi threading:** DOM modifications MUST happen on the JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** use the static instance-set pattern (below) to guard lambdas against use-after-free
- **Includes arrive by declaration, not by luck:** there is no blanket `source/` include root any more. If a header stops resolving after a move, decide whether the dependency is real — repath it — or was never real, and delete the include. Do not add a link edge to make it compile.

## RmlUi (UI Framework)

- Templates are `.rml` (HTML-like), styles `.rcss` (CSS-like); shared ones in `source/framework/juce/jucePluginData/`
- `Rml::String` is `std::string` — no `.c_str()` needed when assigning
- Use `findChildT<Type>()` rather than `findChild` + `dynamic_cast`
- `ElemComboBox` uses `addOption()`, not `addItem()`; `setSelectedIndex(idx, false)` suppresses the callback
- RmlUi has no `contextmenu` event — use `Mousedown` + `juceRmlUi::helper::isContextMenu()`
- `<col>` elements do NOT control table column widths — put width classes on the `<td>` in each `<tr>`. Template rows cloned via `Clone()` keep their CSS classes.

**Threading:** all dialog callbacks (`onConflict`, `updateProgress`, `onMidiReceived`) must dispatch to the message thread. Prefer `callAsync` over timer polling when queueing audio→UI work. `PatchDB` uses a `shared_mutex` (`m_dataSourcesMutex`) — reads are safe from any thread via `shared_lock`, writes need `unique_lock`.

## MIDI Learn System

Files: `framework/juce/jucePluginLib/midiLearnMapping.*` (data structures, serialization, enums), `midiLearnTranslator.*` (learning state machine, presets, routing), `framework/juce/jucePluginEditorLib/settingsMidiLearn.*` (settings page), `pluginEditorState.cpp` (context menu).

**Parameter overlays** — `jucePluginEditorLib/parameterOverlay.*` wraps a single control with Lock/Link/MidiLearn indicators; `parameterOverlays.*` manages them and listens for bind/unbind from `RmlParameterBinding`. Overlay divs get `tus-parameteroverlay tus-parameteroverlaytype-{type}`; MIDI Learn uses pseudo-classes `midi-bound` (green), `midi-unbound` (red), `midi-listening` (amber). Styles live in `framework/juce/jucePluginData/tus_default.rcss` — use pseudo-class selectors so synth skins can override. `pointer-events: auto` on the overlay intercepts clicks before the control.

**Value extraction:** CC → `event.c`. PolyPressure → value `event.c`, note `event.b`. ChannelPressure → `event.b` (2-byte message). PitchBend → 14-bit, LSB `event.b`, MSB `event.c`; use MSB for 7-bit, `(c << 7) | (b & 0x7f)` for full resolution.

**Mode detection (CC only):** values 0x00–0x02 *and* 0x7D–0x7F → `RelativeSigned` (1=inc, 127=dec); 0x3E–0x42 → `RelativeOffset` (65=inc, 63=dec); sequential small changes → `Absolute`. PitchBend, ChannelPressure and PolyPressure are always `Absolute`.

**Settings dialog presets:** `m_originalPreset` is saved on open and restored on close if not applied. When editing the "Current" preset it must be re-synced on every change or edits are lost — use `isCurrentPresetSelected()` and `kCurrentPresetName`.

Unit tests: `source/framework/tools/midiLearnTest/midiLearnTest.cpp`.

## Patch Manager & Program Change Routing

**PatchDB** (`framework/juce/jucePluginLib/patchdb/`) is thread-safe via `shared_mutex`. A `DataSource` is a folder, file, local storage or ROM bank, and can carry a `midiBankNumber` for program change routing. Assignments persist in `patchmanagerdb.json` under `midiBankAssignments`, kept separate from `datasources` because ROM sources are not in that array. An assignment referencing a not-yet-loaded ROM source is held in `m_pendingMidiBankAssignments` and applied on `addDataSource()`.

**Program change router** (`framework/juce/jucePluginLib/programChangeRouter.*`) tracks per-channel bank select (CC#0 MSB + CC#32 LSB). Bank Select CCs pass through to the device; only Program Change is consumed, and only when a bank is assigned. Two-stage for thread safety: the audio thread does a read-only `HasBankFunc` check under `shared_lock` and queues a `ProgramChangeRequest`; the UI thread drains it via `callAsync`. Gated by `m_midiRoutingMatrix.enabled(_ev, MidiEventSource::Device)`, and wired in `Processor::addMidiEvent()` after MIDI Learn, before device routing.

**ROM data sources:** only Osirus/OsTIrus and JE8086 register ROM data sources with the patch manager. Vavra, Xenia and NodalRed2x do not (uninitialized flash or no ROM presets). ROM sources get sequential default `midiBankNumber`s; user assignments from JSON override them on reload.

**callAsync safety pattern** — guard against use-after-free with a static instance set:
```cpp
static std::mutex& getMutex() { static std::mutex m; return m; }
static std::set<MyClass*>& getInstances() { static std::set<MyClass*> s; return s; }
// register in ctor, unregister in dtor
// in the callAsync lambda: lock, check the pointer is still in the set before using it
```

## State Save/Restore (DAW Persistence)

**Save:** `Processor::getStateInformation()` → `saveChunkData()` → `Plugin::getState()` → `Device::getState()`. The device produces `std::vector<SMidiEvent>` of sysex dumps, serialized into a flat byte vector and wrapped with a version header by `Plugin::getState()`.

**Restore:** `Processor::setStateInformation()` → `Plugin::setState()` → `Device::setState()`. `splitMultipleSysex()` splits the flat stream back into messages by `0xF0`…`0xF7` pairs and feeds them into the device.

**Per-synth:** Virus uses Single/Multi dumps via the `virusLib` sysex protocol. JE8086 uses `ronaldo/common/Storage` for system + temp performance dumps (`createHeader()` → `Storage::read()` appends body → `createFooter()`). Vavra/Xenia/NodalRed2x use different mechanisms and do not call `Storage::read()`.

**Append semantics — this has caused real regressions.** By the time `Device::getState()` runs, `Plugin::getState()` has already pushed header bytes into `_state`. A device building sysex into a local buffer MUST use `_state.insert(_state.end(), …)`, never `_state.assign(…)`, which overwrites the header. Both JE8086 and Vavra/Xenia shipped this bug in 2.1.2.

## SysexBuffer / PMR Types

In `framework/synthLib/midiTypes.h`: `synthLib::SysexBuffer` is `std::pmr::vector<uint8_t>` (plain `std::vector` on platforms without PMR, e.g. macOS 10.12/10.13, selected by `__has_include(<memory_resource>)`); `SysexBufferList` is `std::vector<SysexBuffer>`; `SMidiEvent::sysex` is a `SysexBuffer`.

Pitfalls: a template overload accepting both `std::vector<uint8_t>` and `SysexBuffer` must preserve identical semantics (append vs replace, size handling) — this is exactly how the 2.1.2 state bug got in. Implicit conversion between the two works through iterator constructors but copies, so watch hot paths. The save/restore path converts at the `Plugin`/`Device` boundary.

## Voice Expansion (Xenia/Vavra)

The Microwave II/XT and microQ support voice expansion via extra DSP boards; in emulation that means several DSP56300 instances.

- `XT_VOICE_EXPANSION` in `waldi/xt/xtLib/xtBuildconfig.h` enables multi-DSP mode; `g_dspCount` = 3, `g_mainDspIdx` = 2 (main DSP is last, matching hardware)
- DSPs form an **ESSI1 ring bus**: DSP2(main) TX → DSP0 RX → DSP0 TX → DSP1 RX → DSP1 TX → DSP2 RX
- **ESSI0** is audio I/O (DAC/ADC), active only on the main DSP; **ESSI1** is the inter-DSP bus at 8× the audio rate (320kHz vs 40kHz)
- `xtHardware::initVoiceExpansion()` sets up the boot pump, ESSI1 ring routing and the ESSI0 callback
- ESSI1 routing uses real-time TX callbacks, not batch routing — each DSP's `writeTXimpl` forwards straight to the next DSP's RX, which gives natural semaphore backpressure
- Boot order: expansion DSPs first, firmware handshake via magic value `$535400` over ESSI1
- `EsxiClock::exec()` processes ALL TX then ALL RX — the ordering matters when reasoning about data flow
- `RingBuffer` in audio mode blocks on semaphores (`push_back`/`pop_front`) even though `waitNotEmpty`/`waitNotFull` are no-ops
- Expansion DSPs have ESSI0 TE=0/RE=0, so no audio throttle — ESSI1 backpressure is the only one. Main DSP has SCKD=1 on ESSI1 (clock master), expansion DSPs SCKD=0 (slave).

## CI/CD

**GitHub Actions** (`.github/workflows/`): `cmake.yml` (matrix: Ubuntu, macOS 14, Windows 2022, default + Ninja generators), `nightly.yml`, `release.yml`. Linux CI deps: `sudo apt install -y libgl1-mesa-dev xorg-dev libasound2-dev`.

**Jenkins** (private) — three jobs:
- **`dsp56300_main`** — single-platform build, Jenkinsfile from SCM (`scripts/Jenkinsfile`). Stages: Checkout → Compile → Pack → Integration Tests → Deploy → Upload → GitHub. Params: `Branch`, `AgentLabel`, `Synths` (cmake `-D` flag string), `DisplayName`, `FXPlugins`, `Deploy`, `Upload`, `GitHub`, `IntegrationTests`, `UploadFolder`.
- **`dsp56300_main_multi`** — multi-platform orchestrator, inline pipeline (`scripts/JenkinsfileMulti`). Triggers `dsp56300_main` in parallel per platform; per-synth booleans (`SynthOsirus`, `SynthOsTIrus`, `SynthVavra`, `SynthXenia`, `SynthNodalRed2x`, `SynthJe8086`, `DSPBridge`) are assembled into the `Synths` string. `UploadFolder`: `internal`, `alpha`, `beta`, `donators`. Posts an MQTT notification on completion.
- **`dsp56300_copy`** — rclone archival of build artifacts.

Agent labels: `win`, `mac`, `linux && arm`, `linux && x86`.

## YouTrack Issue Tracker

**BUG — "TUS Bug and Feature Reporting"** (public-facing)
- Type: Question, Incident, Problem, Task, Feature Request
- State: New → In Progress → Review → Solved (also Pending, On hold, Duplicate, No change needed, Rejected)
- Priority: Urgent, High, Normal, Low
- Required multi-selects at creation: **Emulator**, **Operating System** (Windows/MacOS/Linux/All), **DAW** (Cubase, Ableton Live, Logic Pro, Bitwig, FL Studio, Reaper, Studio One, Other, All), **Plugin Format** (AU, CLAP, LV2, VST2, VST3, All)

**EMU — "TheUsualSuspects"** (internal)
- Type: Bug, Cosmetics, Exception, Feature, Task, Usability Problem, Performance Problem, Epic
- Stage: Backlog → TODO → In Progress → Review → Staging → Done
- Priority: Show-stopper, Critical, Major, Normal, Minor
- Subsystem: RmlUI C++, Skin, Framework, dsp56000, MC68331

**When a ticket is done:** set State/Stage to **Review**, assign to **bax**, and set **Fixed in Version** (BUG) or **Fixed in build** (EMU) to the current version from `CMakeLists.txt` (`project(gearmulator VERSION x.y.z)`). The Emulator/Product field tells you which source directories are relevant — see the per-synth table above.

## Release Workflow

**Hotfix:**
1. `git checkout oss/main` (tracks `gearmulator/main`)
2. Apply the minimal fix, commit
3. Add a new version section to `doc/changelog.txt`
4. `git push gearmulator oss/main:main`
5. `git tag <version>` and push with `--tags`
6. Move *feature* tickets from the hotfix version to the next version; *bug fix* tickets stay

`doc/changelog.txt` is the source of truth for release notes.

**Changelog conventions:** `- [Fix] description` for fixes, `- [Imp] description` for improvements. Continuation lines indent 8 spaces. Group by version, then by section — `Framework:` for shared changes, then per-synth (`Osirus:`, `JE8086:`, `Vavra/Xenia:`). Framework entries apply to all synths.
