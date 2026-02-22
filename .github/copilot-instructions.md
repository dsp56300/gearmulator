# Gearmulator Development Guide

Gearmulator emulates classic virtual analog synthesizers from the late 90s/2000s by emulating the original ICs and running authentic firmware (ROMs) as audio plugins. Supported devices include Access Virus (A/B/C/TI/Snow), Waldorf microQ/Microwave II/XT, Clavia Nord Lead/Rack 2x, and Roland JP-8000.

## Build System

### CMake Options

Configure which synths to build:
- `gearmulator_SYNTH_OSIRUS` - Access Virus A/B/C (default: on)
- `gearmulator_SYNTH_OSTIRUS` - Access Virus TI/TI2/Snow (default: on)
- `gearmulator_SYNTH_VAVRA` - Waldorf microQ (default: on)
- `gearmulator_SYNTH_XENIA` - Waldorf Microwave II/XT (default: on)
- `gearmulator_SYNTH_NODALRED2X` - Clavia Nord Lead/Rack 2x (default: on)
- `gearmulator_SYNTH_JE8086` - Roland JP-8000 (default: on)

Configure plugin formats:
- `gearmulator_BUILD_JUCEPLUGIN` - Build JUCE-based plugins (default: on)
- `gearmulator_BUILD_JUCEPLUGIN_CLAP` - Build CLAP format (default: on)
- `gearmulator_BUILD_JUCEPLUGIN_LV2` - Build LV2 format (default: on)
- `gearmulator_BUILD_FX_PLUGIN` - Build FX variants (default: off)

### Build Commands

**Windows:**
```bash
cmake . -B temp/cmake_win64 -G "Visual Studio 15 2017 Win64"
cmake --build temp/cmake_win64 --config Release -j 4
cd temp/cmake_win64 && cpack -G ZIP
```

**Current Development Setup:**
- Build directory: `.\temp\cmake_vs26`
- Contains the Visual Studio 2026 solution currently in use
- When building or testing, use this directory

**Linux:**
```bash
cmake . -B temp/cmake_linux -Dgearmulator_BUILD_JUCEPLUGIN=ON -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=ON
cmake --build temp/cmake_linux --config Release -j 4
cd temp/cmake_linux && cpack -G DEB  # or RPM or ZIP
```

**macOS:**
```bash
cmake -G Xcode -S . -B temp/cmake
cmake --build temp/cmake --config Release
cd temp/cmake && cpack -G ZIP
```

Convenience build scripts exist: `build_win64.bat`, `build_linux.sh`, `build_mac.sh`

### Tests

Run integration tests:
```bash
ctest -C Release
```

Individual test consoles exist for each synth:
- `virusIntegrationTest` - Virus integration tests
- `virusTestConsole` - Virus test console
- `mqTestConsole` - Waldorf microQ test console
- `xtTestConsole` - Waldorf Microwave II/XT test console
- `n2xTestConsole` - Nord Lead 2x test console
- `jeTestConsole` - JP-8000 test console

## Architecture

### Core Emulation Layers

1. **DSP Emulation** (`dsp56300/`)
   - Motorola DSP 56300 family emulator at the core
   - Used by Access Virus, Waldorf Q/microQ, Nord Lead 3, Novation Supernova
   - JIT compilation via asmjit for performance
   - Peripherals: ESAI, HDI08 (Host Device Interface)

2. **MC68K Emulation** (`mc68k/`)
   - Motorola 68000 family emulator using Musashi
   - Used by devices requiring 68K microcontrollers

3. **Device Libraries** (per-synth)
   - `virusLib/` - Access Virus A/B/C/TI/Snow implementation
   - `mqLib/` - Waldorf microQ implementation
   - `xtLib/` - Waldorf Microwave II/XT implementation
   - `nord/n2x/` - Clavia Nord Lead/Rack 2x implementation
   - `ronaldo/je8086/` - Roland JP-8000 implementation

4. **Hardware Abstraction** (`hardwareLib/`)
   - Common hardware peripherals (LCD, buttons, encoders)
   - Hardware interface abstractions

5. **Common Libraries**
   - `baseLib/` - Low-level utilities (filesystem, logging, events, binary streams)
   - `synthLib/` - Cross-synth audio plugin infrastructure (Device base class, DAC, resampling, MIDI routing, parameter system)

### Plugin Architecture

Each synth has a JUCE plugin implementation:
- `osirusJucePlugin/` - Osirus (Virus A/B/C) plugin
- `osTIrusJucePlugin/` - OsTIrus (Virus TI) plugin
- `mqJucePlugin/` - Vavra (microQ) plugin
- `xtJucePlugin/` - Xenia (Microwave II/XT) plugin
- `nord/n2x/n2xJucePlugin/` - Nodal Red 2x (Nord Lead) plugin
- `ronaldo/je8086/jeJucePlugin/` - JE-8086 (JP-8000) plugin

**Key Plugin Components:**
- `jucePluginLib/` - Core JUCE plugin infrastructure (Parameter system, processor base, MIDI handling, controller mapping)
- `juceRmlUi/` - RmlUi integration for declarative UI (HTML/CSS-like)
- `juceUiLib/` - Common UI components
- `jucePluginEditorLib/` - Plugin editor infrastructure
- `jucePluginData/` - Shared plugin assets

**Plugin Build Flow:**
1. Each synth plugin calls `createJucePluginWithFX()` macro from `juce.cmake`
2. Links device-specific library (e.g., `virusJucePlugin` links `virusLib`)
3. Processor inherits from `synthLib::Plugin` which wraps `synthLib::Device`
4. Skins defined via RML/RCSS files, compiled into binary data
5. Multiple skins per synth using `addSkin()` and `buildSkinHeader()` macros

### Device Model Pattern

All synth implementations follow this pattern:

```cpp
namespace <synthName>Lib {
    class Device : public synthLib::Device {
        // DSP/MC68K instance
        // Microcontroller for MIDI/UI
        // ROM loading and memory setup
        // Audio I/O via HDI08 or similar
    };
}
```

Key responsibilities:
- Load firmware ROM files
- Initialize DSP/CPU memory
- Handle MIDI via microcontroller or direct DSP communication
- Process audio blocks (typically via HDI08 interface on DSP)
- Manage front panel state synchronization

## Code Conventions

### Style
- **Use tabs for indentation** (tab size: 4, UseTab: Always)
- **Column limit: 120 characters**
- **Braces**: Custom style with braces on new lines for all constructs
- **Namespaces**: All namespace content indented
- **Prefer early outs over deep nesting** - Use guard clauses and early returns to reduce nesting depth
- **Respect existing coding style** - When working in a file, match its existing patterns and conventions
- **Function parameters MUST have underscore prefix**: `void func(int _param, const std::string& _name)`
- Formatted via `.clang-format` in `source/` directory

### Naming
- Namespaces: `virusLib`, `synthLib`, `baseLib`, `dsp56k`
- Classes: `PascalCase` (e.g., `Device`, `MidiBufferParser`)
- Variables/functions: `camelCase` (e.g., `processAudio`, `sampleRate`)
- Member variables: `m_` prefix (e.g., `m_statusText`, `m_isConflict`)
- Constants/enums: Context-dependent

### File Organization
- Device implementations in `<synth>Lib/` directories
- JUCE plugins in `<synth>JucePlugin/` directories
- Headers and implementation files co-located
- CMakeLists.txt per module

### Platform-Specific Considerations
- **Windows**: MSVC with `/O2 /GS- /fp:fast /Oy /GT /GL /Zi /Oi /Ot` for Release
- **Linux**: `-Ofast -fno-stack-protector`, LTO enabled (except GCC due to bugs)
- **macOS**: Universal binaries (x86_64 + arm64), minimum 10.12/10.13, `-Ofast -flto`
- **ARM**: Special handling for ARMv8.1a+ atomics on known boards (rk3588, rock-5b, rpi-2712)
- C++17 required (extensive compile-time metaprogramming in DSP emulator)

## Key Build Files

- `base.cmake` - Compiler flags and platform-specific settings for all targets
- `source/juce.cmake` - JUCE plugin configuration and format selection
- `source/skins.cmake` - Skin asset compilation system
- `source/exporttarget.cmake` - Target export utilities
- `source/changelog.cmake` - Changelog generation
- `scripts/pack.cmake` - CPack packaging script

## Dependencies

Managed as git submodules or in-tree:
- **JUCE** - Audio plugin framework (VST3, AU, FST, CLAP, LV2)
- **RmlUi** - HTML/CSS-like UI framework
- **asmjit** - JIT compiler for DSP emulation
- **portaudio/portmidi** - Audio/MIDI I/O for test consoles
- **libresample** - Audio resampling
- **cpp-terminal** - Terminal UI for test consoles
- **clap-juce-extensions** - CLAP plugin support for JUCE
- **Musashi** - MC68000 emulator (in `mc68k/`)

## CI/CD

GitHub Actions workflows (`.github/workflows/`):
- `cmake.yml` - Build matrix: Ubuntu, macOS 14, Windows 2022 with default/Ninja generators
- `nightly.yml` - Nightly builds
- `release.yml` - Release builds

Linux dependencies for CI:
```bash
sudo apt install -y libgl1-mesa-dev xorg-dev libasound2-dev
```

## Network Bridge

Optional network bridge for remote hardware communication (`bridge/`, `networkLib/`, `ptypes/`).

## Working with the Codebase

When implementing changes:
1. **Device-level changes** go in `<synth>Lib/` (e.g., `virusLib/device.cpp`)
2. **Plugin UI changes** go in `<synth>JucePlugin/` (RML/RCSS for layout, processor for logic)
3. **Shared plugin infrastructure** changes go in `jucePluginLib/` or `synthLib/`
4. **DSP emulator fixes** go in `dsp56300/source/dsp56kEmu/`
5. Test changes locally with test consoles before building full plugins
6. Use existing `*TestConsole` applications for rapid DSP/device iteration

When adding a new parameter:
1. Update `parameterDescriptions_*.json` in the synth plugin directory
2. Map to device-specific MIDI CC or SysEx in processor
3. Update UI skin RML if exposing in UI

The project emphasizes **accuracy of emulation** over shortcuts - the goal is to run original firmware bit-identically to hardware.

### ROM/Factory Data Sources
- Only **Virus** (Osirus/OsTIrus) and **JE8086** register ROM data sources with the patch manager
- Vavra, Xenia, NodalRed2x do NOT add ROM datasources (uninitialized flash or no ROM presets)
- ROM data sources get default `midiBankNumber` assignments (sequential, starting from 0)
- User-assigned bank numbers from JSON override defaults on reload

## RmlUi (UI Framework)

The plugin UI uses RmlUi, an HTML/CSS-like framework. Key details:

### RML/RCSS Files
- Templates defined in `.rml` files (HTML-like), styles in `.rcss` files (CSS-like)
- Located in `source/jucePluginData/` for shared templates
- `Rml::String` is `std::string` — no `.c_str()` needed when assigning
- Use `findChildT<Type>()` instead of `findChild` + `dynamic_cast`
- `ElemComboBox` uses `addOption()` not `addItem()`
- `setSelectedIndex` has optional bool to suppress callback: `setSelectedIndex(idx, false)`

### RmlUi Limitations
- `<col>` elements do NOT work for controlling table column widths — apply width classes directly to `<td>` elements in each `<tr>` instead
- Table column template rows are cloned via `Clone()` — CSS classes on `<td>` elements are preserved in clones

### Threading
- RmlUi DOM modifications MUST happen on the JUCE message thread
- MIDI callbacks run on the audio/MIDI thread — always use `juce::MessageManager::callAsync` when updating UI from MIDI callbacks
- All dialog callbacks (`onConflict`, `updateProgress`, `onMidiReceived`) must dispatch to message thread
- `PatchDB` uses `shared_mutex` (`m_dataSourcesMutex`) — read operations (like `getDataSourceByMidiBankNumber`) are safe from any thread via `shared_lock`; write operations need `unique_lock`
- When queueing work from audio thread to UI thread, prefer `juce::MessageManager::callAsync` over timer-based polling for lower latency
- Use the static instance-set pattern (see Patch Manager section) to guard `callAsync` lambdas against use-after-free

## MIDI Learn System

### Architecture
- `jucePluginLib/midiLearnMapping.h/.cpp` — Mapping data structures, serialization, mode/type enums
- `jucePluginLib/midiLearnTranslator.h/.cpp` — Core engine: learning state machine, preset management, MIDI routing
- `jucePluginEditorLib/settingsMidiLearn.h/.cpp` — Settings UI page
- `juceRmlUi/rmlMidiLearnDialog.h/.cpp` — Learning/conflict dialog
- `jucePluginEditorLib/pluginEditorState.cpp` — Context menu integration

### MIDI Message Value Extraction
- **CC**: value in `event.c`
- **PolyPressure**: value in `event.c`, note in `event.b`
- **ChannelPressure**: value in `event.b` (2-byte message)
- **PitchBend**: 14-bit value, LSB in `event.b`, MSB in `event.c`. For 7-bit use MSB (`event.c`), for full resolution: `(c << 7) | (b & 0x7f)`

### Mode Detection (CC only)
- Values 0x00-0x02 AND 0x7D-0x7F → `RelativeSigned` (1=inc, 127=dec)
- Values 0x3E-0x42 (around center 0x40) → `RelativeOffset` (65=inc, 63=dec)
- Sequential small changes → `Absolute`
- PitchBend, ChannelPressure, PolyPressure are always `Absolute`

### Settings Dialog Preset Workflow
- `m_originalPreset` saved on dialog open, restored on close if not applied
- When editing "Current" preset, `m_originalPreset` must be synced on each change (otherwise edits are lost)
- Use `isCurrentPresetSelected()` helper and `kCurrentPresetName` constant

### Unit Tests
- Test binary: `source/midiLearnTest/midiLearnTest.cpp`
- Built to: `temp/cmake_vs26/source/midiLearnTest/midiLearnTest_artefacts/Debug/midiLearnTest.exe`

## YouTrack Issue Tracker

Two YouTrack projects are used:

### BUG Project — "TUS Bug and Feature Reporting" (public-facing)
- **Type**: Question, Incident, Problem, Task, Feature Request
- **State**: New → In Progress → Review → Solved (also: Pending, On hold, Duplicate, No change needed, Rejected)
- **Priority**: Urgent, High, Normal, Low
- **Emulator** (required, multi-select): Osirus, OsTirus, Vavra, Xenia, NodalRed2x, JE8086, DSPBridge, All Emulators
- **Operating System** (required, multi-select): Windows, MacOS, Linux, All
- **DAW** (required, multi-select): Cubase, Ableton Live, Logic Pro, Bitwig Studio, FL Studio, Reaper, Studio One, Other, All
- **Plugin Format** (required, multi-select): AU, CLAP, LV2, VST2, VST3, All
- **Fixed in Version**: Set to the current project version from `CMakeLists.txt` (`project(gearmulator VERSION x.y.z)`) — currently **2.1.3**

### EMU Project — "TheUsualSuspects" (internal development)
- **Type**: Bug, Cosmetics, Exception, Feature, Task, Usability Problem, Performance Problem, Epic
- **Stage**: Backlog → TODO → In Progress → Review → Staging → Done
- **Priority**: Show-stopper, Critical, Major, Normal, Minor
- **Product** (multi-select): Osirus, OsTIrus, Vavra, Xenia, Nodal Red 2X, JE8086, All
- **Subsystem**: RmlUI C++, Skin, Framework, dsp56000, MC68331
- **Affected versions** / **Fixed in build**: Version strings

### Emulator ↔ Source Directory Mapping

| Emulator Name | Hardware | Device Library | Plugin Directory |
|---|---|---|---|
| Osirus | Access Virus A/B/C | `virusLib/` | `osirusJucePlugin/` |
| OsTIrus | Access Virus TI/TI2/Snow | `virusLib/` | `osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `mqLib/` | `mqJucePlugin/` |
| Xenia | Waldorf Microwave II/XT | `xtLib/` | `xtJucePlugin/` |
| NodalRed2x | Clavia Nord Lead/Rack 2x | `nord/n2x/` | `nord/n2x/n2xJucePlugin/` |
| JE8086 | Roland JP-8000 | `ronaldo/je8086/` | `ronaldo/je8086/jeJucePlugin/` |
| DSPBridge | Network bridge | `bridge/` | — |

### Workflow Notes
- **When a ticket is marked done**: Set State to **Review**, assign to **bax**, and set **Fixed in Version** to the current `CMakeLists.txt` project version
- The **Emulator** / **Product** field determines which source directories are relevant
- BUG project requires Emulator, Operating System, DAW, and Plugin Format fields at creation

## Patch Manager & Program Change Routing

### PatchDB Architecture
- `jucePluginLib/patchdb/` — Core patch database (thread-safe, uses `shared_mutex` for data source access)
- `DataSource` represents a folder, file, local storage, or ROM bank of patches
- Each `DataSource` can have a `midiBankNumber` assignment for MIDI program change routing
- Bank assignments persist in `patchmanagerdb.json` under `midiBankAssignments` (separate from `datasources` array because ROM sources aren't in that array)
- Pending assignments: if a bank assignment references a ROM DataSource not yet loaded, it's stored in `m_pendingMidiBankAssignments` and applied when `addDataSource()` is called

### Program Change Router (`jucePluginLib/programChangeRouter.h/.cpp`)
- Tracks per-channel bank select state (CC#0 MSB + CC#32 LSB)
- Bank Select CCs pass through to device (not consumed); only Program Change is consumed when a bank is assigned
- Two-stage design for thread safety:
  - **Audio thread**: `HasBankFunc` callback does read-only check via `shared_lock` — safe
  - **Audio thread**: queues `ProgramChangeRequest` into mutex-protected vector
  - **UI thread**: `PatchManager` drains queue via `juce::MessageManager::callAsync`
- Gated by `m_midiRoutingMatrix.enabled(_ev, MidiEventSource::Device)` — respects user's MIDI matrix settings
- Wired in `Processor::addMidiEvent()` after MIDI Learn, before routing to device

### callAsync Safety Pattern
When using `juce::MessageManager::callAsync` with class instances, use a **static instance set** to guard against use-after-free:
```cpp
static std::mutex& getMutex() { static std::mutex m; return m; }
static std::set<MyClass*>& getInstances() { static std::set<MyClass*> s; return s; }
// Register in ctor, unregister in dtor
// In callAsync lambda: lock mutex, check if pointer still in set before using
```

## State Save/Restore (DAW Persistence)

### Save Path
`Processor::getStateInformation()` → `saveChunkData()` → `Plugin::getState()` → `Device::getState()`
- Device produces `std::vector<SMidiEvent>` containing sysex dumps
- Events are serialized into a flat `std::vector<uint8_t>` by concatenating all sysex bytes
- Wrapped with version header by `Plugin::getState()`

### Restore Path
`Processor::setStateInformation()` → `Plugin::setState()` → `Device::setState()`
- `splitMultipleSysex()` splits flat byte stream back into individual messages by finding `0xF0`...`0xF7` pairs
- Messages are fed back into the device's state/MIDI input

### Per-Synth State Patterns
- **Virus (Osirus/OsTIrus)**: Single/Multi dumps via `virusLib` sysex protocol
- **JE8086**: Uses `ronaldo/common/Storage` class for system + temp performance dumps; `createHeader()` → `Storage::read()` (appends body) → `createFooter()`
- **Vavra/Xenia/NodalRed2x**: Different state mechanisms, don't use `Storage::read()`

### Critical: Storage::read() Append Semantics
`Storage::read(vector, addr, count)` **appends** data to the destination vector (uses `push_back`). The template overload for non-`std::vector<uint8_t>` types (e.g., PMR vectors) must also append via `insert()`, NOT replace via `assign()`. This was the root cause of the 2.1.2 JE8086 state save regression.

## SysexBuffer / PMR Types

### Type Aliases (in `synthLib/midiTypes.h`)
- `synthLib::SysexBuffer` = `std::pmr::vector<uint8_t>` (or `std::vector<uint8_t>` on platforms without PMR, e.g., macOS 10.12/10.13)
- `synthLib::SysexBufferList` = `std::vector<SysexBuffer>`
- `SMidiEvent::sysex` is `SysexBuffer`
- Compile-time check: `__has_include(<memory_resource>)` determines availability

### PMR Pitfalls
- When adding template overloads that accept both `std::vector<uint8_t>` and `SysexBuffer`, ensure the template preserves the same semantics (append vs replace, size handling, etc.)
- Implicit conversions between `std::vector<uint8_t>` and PMR vector work via iterator constructors but create copies — be mindful in hot paths
- The save/restore path converts between PMR sysex and `std::vector<uint8_t>` at the `Plugin`/`Device` boundary

## Release Workflow

### Hotfix Process
1. Switch to the `oss/main` branch (tracks `gearmulator/main`): `git checkout oss/main`
2. Apply minimal fix, commit
3. Update `doc/changelog.txt` with new version section
4. Push: `git push gearmulator oss/main:main`
5. Tag: `git tag <version>` and push with `--tags`
6. Move feature tickets from the hotfix version to next version (e.g., 2.1.3 → 2.1.4)
7. Bug fix tickets stay in the hotfix version

### Version Management
- Feature tickets use **"Fixed in build"** (EMU) or **"Fixed in Version"** (BUG) fields
- When releasing a hotfix, bump feature tickets to next minor version
- Bug fixes that are already on main stay in the hotfix version
- `doc/changelog.txt` is the source of truth for release notes

### Git Remotes
- `gearmulator` — public OSS repo (dsp56300/gearmulator on GitHub)
- `private` — private development repo
- Also: `nas`, `codeberg`, `EvilDragon`

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval
- User stages changes themselves — only commit when asked
