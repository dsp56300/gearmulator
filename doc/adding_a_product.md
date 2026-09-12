# Adding a new product (synth or FX)

The reference for bringing a new emulated device into Gearmulator, written for Claude Code
sessions. Work through the sections in order, read the traps of a section before touching that
area, and tick the checklist in §18.

It builds on `CLAUDE.md` (layout, conventions, release workflow) and does not repeat the deep
dives: [skin_authoring.md](skin_authoring.md), [lua_scripting.md](lua_scripting.md),
[mcp_server.md](mcp_server.md), [restructure_plan.md](restructure_plan.md) §5 (codenames) and §9
(public name gate).

Snapshot of `oss/main` at 2.2.19 (September 2026). Files and symbols are named, line numbers are
not, because they drift. Where this doc and the code disagree, the code wins; fix the doc.

> This file is meant for the public `oss/main`. Keep it free of unreleased device names and of
> every credential. Credentials live in the user's memory, never in the repository.

## Contents

1. [How the emulator works](#1-how-the-emulator-works)
2. [Ground rules](#2-ground-rules)
3. [Map of a product](#3-map-of-a-product)
4. [Names and identifiers](#4-names-and-identifiers)
5. [Device library](#5-device-library)
6. [Custom MIDI](#6-custom-midi)
7. [DSP bridge](#7-dsp-bridge)
8. [Plugin processor](#8-plugin-processor)
9. [Controller](#9-controller)
10. [Parameter descriptions and midi packets](#10-parameter-descriptions-and-midi-packets)
11. [Parts](#11-parts)
12. [Patch manager](#12-patch-manager)
13. [Editor and skin](#13-editor-and-skin)
14. [Build, packaging, changelog](#14-build-packaging-changelog)
15. [CI and branches](#15-ci-and-branches)
16. [Infrastructure outside the repository](#16-infrastructure-outside-the-repository)
17. [Verification](#17-verification)
18. [Checklist](#18-checklist)

---

## 1. How the emulator works

Summary of [How the Emulator Works](https://theusualsuspects.io/technical/how-it-works)
(website repository `../dsp56300.github.io`, page `technical/how-it-works.md`):

- **Hardware emulation, not a software synth.** Chips are emulated instruction by instruction
  together with their peripherals (DSP56300 family with timers, ESAI/ESSI, DMA, HDI08; 68k; H8S;
  custom ASICs). The original firmware ROM runs unmodified - never patched or extended - so the
  output is bit-exact, quirks and bugs included. A microcontroller is either emulated or, where
  full emulation buys nothing, reimplemented in C++ (the Virus i8051).
- **The UI is a MIDI controller.** A knob sends CC or sysex to the emulated device, the firmware
  processes it like MIDI from a real port, and the change comes back as MIDI that the UI
  reflects. The device cannot tell the editor from a DAW or a keyboard.
- **Custom MIDI extensions** carry what real MIDI cannot: LCD contents, LED states, LFO phases,
  button and encoder presses. They are emulator-only and never reach the DAW or external ports.
- **The patch manager** sends complete dumps to the edit buffer instead of using the device's
  own banks, so thousands of presets can be managed.
- **MIDI routing matrix** with four endpoints: Device, Editor, Host (DAW), Physical (external
  ports). Internal messages travel between Device and Editor only.
- **UI** is a JUCE plugin shell with RmlUi rendering skins written in `.rml`/`.rcss`; a product
  can have several skins.
- **Everything runs through MIDI.**

```
 Host (DAW) ─┐                                   ┌─ Physical MIDI ports
             ▼                                   ▼
        ┌────────────────── MIDI routing matrix ──────────────────┐
        ▼                                                         ▼
 Emulated device  ◄──── MIDI + emulator-only sysex ────►  Editor (controller + skin)
 firmware on DSP / MCU                                    parameters, LCD, LEDs, patches
        │
        ▼ audio
```

---

## 2. Ground rules

1. **All controller↔device communication is MIDI.** Controller and editor never call into the
   device object. This is not negotiable, because:
   - With the DSP bridge the device runs in another process or on another machine
     (`bridgeClient::RemoteDevice`). The wire carries MIDI, audio, a state blob, the sample
     rate, the DSP clock and the creation parameters, nothing else (§7).
   - The device is replaced at runtime: ROM or model switch, voice-expansion reboot, remote
     fallback, reconnect. The controller outlives it, so any pointer goes stale. The controller
     resyncs by sending requests from `onStateLoaded()`.
   - The controller runs on the message thread, the device on the audio thread.
     `Plugin::addMidiEvent` is the thread-safe, sample-ordered way in.
   - The same path serves host automation, physical ports, the patch manager, MIDI Learn,
     program change routing, DAW state, the MCP server and the test consoles.

   No plugin code touches a concrete device class today; keep it that way. Test consoles may
   call device C++ APIs because they are headless and local.
2. **What real MIDI cannot express becomes custom sysex** (§6): front panel, LCD, LEDs, pot
   positions, emulator-only settings.
3. **The controller is a MIDI editor.** It requests the complete state from the device, parses
   the replies into parameters, displays them, sends edits as MIDI, and reads back after sending
   a whole patch (§9).
4. **Describe message layouts as `midipackets`** in the parameter description JSON and build and
   parse them with `pluginLib::MidiPacket` (§10). Write C++ only where the DSL cannot express the
   protocol; JE8086's address-based Roland DT1 messages with 14-bit values are the example.
5. **Part = multitimbral part** (§11). Never an oscillator, layer or envelope.
6. **Accuracy over shortcuts.** Firmware quirks are handled in the device library. The ROM is
   never modified.
7. **Device state is concatenated sysex, and `getState` appends** (`insert`, never `assign`;
   `CLAUDE.md` "State Save/Restore").
8. **Names.** Product name rules in §4, manufacturer folder codenames per restructure_plan.md §5,
   no unreleased device names in anything that lands on `oss/main`, commit messages included.

---

## 3. Map of a product

`<maker>` is the manufacturer codename folder (`axel`, `waldi`, `claudia`, `ronaldo`, or a new
one), `<fam>` the family folder, `<x>` the short prefix of the target names.

| Piece | Location | Base class / entry | Example to copy |
|---|---|---|---|
| Device library | `source/<maker>/<fam>/<x>Lib/` | `synthLib::Device` (`wLib::Device` for Waldorf-style 68k + DSP) | `claudia/n2x/n2xLib`, `ronaldo/je8086/jeLib` |
| Custom sysex codec | device library | `synthLib::SysexRemoteControl` | `jeLib/sysexRemoteControl.*` |
| ROM loader | device library | `synthLib::RomLoader` | `jeLib/romloader.*` |
| Test console | `source/<maker>/<fam>/<x>TestConsole/` | executable using the device | `ronaldo/je8086/jeTestConsole` (target `JE8086TestConsole`) |
| Processor | `source/<maker>/<fam>/<x>JucePlugin/` | `jucePluginEditorLib::Processor` | `jePluginProcessor.*` |
| Controller | plugin directory | `pluginLib::Controller` | `n2xController.*`, `xtController.*` |
| Parameter descriptions | plugin directory, `parameterDescriptions_<x>.json` | `pluginLib::ParameterDescriptions` | `parameterDescriptions_n2x.json` |
| Editor, editor state | plugin directory | `jucePluginEditorLib::Editor`, `PluginEditorState` | `jeEditor.*` (the most compact) |
| Patch manager | plugin directory | `jucePluginEditorLib::patchManager::PatchManager` | `jePatchManager.*` |
| Skins | plugin directory, `skins/<name>/` | RML + RCSS | `skins/jeTrancy` (hand-written RML) |
| Bridge entry | plugin directory, `serverPlugin.cpp` | `createBridgeDevice()` | every plugin directory |
| CMake | `source/CMakeLists.txt`, `source/<maker>/CMakeLists.txt`, per directory | `createJucePlugin` / `createJucePluginWithFX` | `claudia/`, `ronaldo/` |

**Message flow**

- Editor → device: `Controller::sendSysEx` / `sendMidiEvent` (source `Editor`) →
  `Processor::addMidiEvent` → routing matrix → `Plugin::addMidiEvent` → `Device::process` →
  `Device::sendMidi`.
- Device → editor: `Device::readMidiOut` (source `Device` or `Internal`) →
  `Processor::processBlock` → `addMidiEvent` → routing matrix →
  `Controller::enqueueMidiMessages` (audio thread) → 1 ms timer → `parseMidiMessage` →
  `parseSysexMessage` / `parseControllerMessage` (message thread).
- Default routes (`synthLib::MidiRoutingMatrix` constructor): Device→Editor, Editor→Device,
  Internal→Editor, Physical→Device, Device→Physical, Host→Device, Host→Editor, Editor→Host,
  Physical→Editor. **Device→Host is off.** A message with source `Device` therefore also reaches
  the physical MIDI out; only source `Internal` stays between device and editor.

---

## 4. Names and identifiers

Settle these before creating any target. Renaming later has already cost products a source
move, a deploy folder, a Discord thread and the users' data folders.

| Identifier | Rules | Where it ends up |
|---|---|---|
| **Product name** (2nd argument of `createJucePlugin`) | Starts with an uppercase letter. **No spaces** (breaks the `<Product>ServerPlugin` target, truncates the macOS bundle id). **No `-`** (the public download page splits asset names on `-`). **No `FX` anywhere** (`juce.cmake` removes every "FX" to find the changelog, `Processor::getProductName` strips it for the data folder). **Not a substring of another product name** (the deploy filter `*<Product>*` would upload the other product's archives too). A codename, never a trademark. | DAW plugin name, binaries, CPack components `<Product>-<Format>`, archives, `<Documents>/The Usual Suspects/<Product>/`, changelog header `<Product>:`, deploy folder, bridge identity, Discord thread, YouTrack value |
| **Slug** | lowercase product name, derived in `scripts/deployAll.cmake` | deploy folder `builds/<slug>/`, website `downloads/<slug>` |
| **CMake option** | `gearmulator_SYNTH_<X>` | `source/CMakeLists.txt`, `scripts/products.cmake`, `scripts/JenkinsfileMulti` |
| **Plugin 4CC** | unique across all products and FX variants on all branches; convention `T` + three characters | VST2 unique id, VST3 class ids, AU subtype, CLAP id `com.theusualsuspects.<4CC>`, AU validation, bridge matching |
| **FX 4CC** | a second unique code, if there is an FX variant | same, for `<Product>FX` |
| Manufacturer code | always `TusP`, fixed in `juce.cmake` | - |
| Target prefix `<x>` | unique: `<x>Lib`, `<x>JucePlugin`, `<x>TestConsole` | CMake |

4CCs on `oss/main`, never reuse: Osirus `TusV` / FX `TusF`, OsTIrus `Ttip` / `Ttif`, Vavra
`Tmqs` / `Tmqf`, Xenia `Txts` / `Txtf`, NodalRed2x `Tn2x`, JE8086 `Tjpv`, 88emuPlayer `TscH`.
Private branches use more; grep `createJucePlugin` on every private remote branch before picking
one (`git grep createJucePlugin <ref> -- source`).

**Never change the product name or a 4CC after anything was deployed.** Saved projects stop
loading the plugin, the bridge stops matching and users lose their data folder.

Display names in README, website and installer catalog may differ ("Nodal Red 2x", "JE-8086");
only the product name matters to build and deploy.

---

## 5. Device library

### 5.1 CMake

- STATIC library with `target_include_directories(<x>Lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)`,
  linking the CPU cores and `hardwareLib` / `wLib` / `rLib` as needed. Include what you use
  (restructure_plan.md §8 explains phantom dependencies).
- Family `CMakeLists.txt` like `source/claudia/n2x/CMakeLists.txt`: library, test console, and
  the plugin inside `if(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN)`.
- Maker `CMakeLists.txt` gates each family on its option (`source/claudia/CMakeLists.txt`,
  `source/ronaldo/CMakeLists.txt`). Gate shared dependencies on **every** product that needs
  them; a dependency gated on one product once broke the build of its sibling.

### 5.2 `synthLib::Device`

Construct from `synthLib::DeviceCreateParams` alone (`preferredSamplerate`, `hostSamplerate`,
`romName`, `romData`, `romHash`, `customData`, `homePath`): the bridge server builds the device
from exactly that. Boot completely inside the constructor. Throw
`synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "...")` for a missing or
invalid ROM; the processor catches only `DeviceException` and falls back to a silent
`DummyDevice` with a message box.

Pure virtuals:

| Member | Contract |
|---|---|
| `getSamplerate()` | current device rate |
| `isValid()` | false only when unusable; false at runtime makes `Plugin` replace the device |
| `getState(std::vector<uint8_t>&, StateType)` | **append** concatenated sysex, `Plugin::getState` already wrote a two byte header |
| `setState(const std::vector<uint8_t>&, StateType)` | split with `MidiToSysex::splitMultipleSysex`, feed the state cache; can arrive before any `process()` |
| `getChannelCountIn()` / `getChannelCountOut()` | constant for the plugin's lifetime (the resampler is built once); at most 4 in / 12 out (`TAudioInputs` / `TAudioOutputs`); must match the processor's buses |
| `setDspClockPercent`, `getDspClockPercent`, `getDspClockHz` | forward to the ESAI/ESSI clock, or return false / 100 for a fixed clock |
| `readMidiOut(std::vector<SMidiEvent>&)` | append the device's output |
| `processAudio(inputs, outputs, samples)` | any block size; input pointers can be null when the input count is 0 |
| `sendMidi(const SMidiEvent&, std::vector<SMidiEvent>& response)` | consume one event; immediate replies go into `response` |

Override where relevant: `getSupportedSamplerates` / `getPreferredSamplerates` (ascending, the
first preferred rate ≥ host rate wins), `setSamplerate` (really reconfigure),
`getDynamicSamplerates` (rates the firmware can switch to while running),
`getInternalLatencyMidiToOutput` / `getInternalLatencyInputToOutput` (device-rate samples),
`canModifyDspClock`, `setStateFromUnknownCustomData` (import foreign preset formats),
`onTransportDiscontinuity` (only needed when MIDI input is rate limited; a transport marker must
never reach the emulated UART).

The default `Device::process` hands transport markers to `onTransportDiscontinuity`, runs every
other event through the `MidiTranslator` into `sendMidi`, then calls `processAudio`, then
`readMidiOut`.

### 5.3 Audio, MIDI timing, latency

- Pass `getExtraLatencySamples()` into the audio pump (it pre-fills the receive ring and gives
  the DSP threads headroom) and add it to MIDI scheduling.
- Schedule non-sysex input at samples processed + `getExtraLatencySamples()` + event offset;
  pass sysex whole. Reset the sample counter after boot. Examples: `mqLib/device.cpp`,
  `xtLib/xtDevice.cpp`, `n2xLib/n2xdevice.cpp`.
- Feed the firmware's UART output through `synthLib::MidiBufferParser` with source
  `MidiEventSource::Device`. Emulator-only messages use `MidiEventSource::Internal` (§6).
- `Plugin` inserts MIDI clock (start, stop, timing clock) into the device input by default.
- A Program Change can be consumed by the `ProgramChangeRouter` before it reaches the device,
  when a patch manager bank is assigned to the current bank select.

### 5.4 State

- Concatenated sysex, including emulator-only state (pot positions, master volume) encoded as
  the product's custom sysex.
- Decide what `StateTypeGlobal` and `StateTypeCurrentProgram` mean (Virus: total dump plus
  arrangement versus arrangement only).
- `getState` / `setState` run on the message thread without the plugin lock; protect against a
  concurrent `process()`.
- `setState` must work on a device that is never processed, for example on a frozen track:
  feed the state cache directly.

### 5.5 ROMs

- The processor registers the search paths: `<Documents>/The Usual Suspects/<Product>/roms/`
  (created on first run; the environment variable `TUS_DATA_FOLDER` replaces the Documents
  folder) and the plugin's module directory. Console tools also search the working directory.
- **Detect by content, not by size.** Sizes collide between products; the Virus loader skips
  images it cannot identify for that reason. Accept firmware update `.mid` files if the
  manufacturer shipped operating system updates as MIDI.
- Several ROMs or models: keep the list in the processor, persist the choice in a processor
  chunk, pass the model in `customData`.
- `getRemoteDeviceParams` fills `romData`, `romName` and `customData`; make `createDevice()`
  build from the same parameters so local and remote devices are identical.

### 5.6 Threads

- DSP threads (`dsp56k::DSPThread`), microcontroller thread(s), audio thread. The rings in
  `dsp56k::Audio` are single producer, single consumer, with blocking semaphores: the audio
  thread waits for DSP output.
- Microcontroller/DSP synchronisation to copy: `wLib::Hardware` (`syncUcToDSP`,
  `ucYieldLoop`), `n2x::Hardware` (semaphore permits per chunk), JE8086's `JeThread` (job queue).
- The destructor must unblock every thread it owns (notify semaphores, push dummy frames or
  jobs), or the host hangs when the plugin is removed.
- Boot runs in the constructor, during host instantiation, under the processor's device creation
  mutex. A boot loop without a timeout hangs the DAW; a timeout must leave the device invalid,
  not half initialised.

### 5.7 Test console

Copy `ronaldo/je8086/jeTestConsole`: find the ROM with the product's loader, fill
`DeviceCreateParams`, construct the device inside `try` / `catch (synthLib::DeviceException&)`,
loop `process()`, decode `midiOut` with the custom sysex codec to detect boot and read the LCD,
press buttons with its encoders, write audio with `synthLib::AsyncWriter`, print the real-time
percentage. Install it as component `<Product>TestConsole` (with `createMacSetupScript` /
`installMacSetupScript`) if it should ship as the performance test the website links to.

---

## 6. Custom MIDI

Use custom sysex for everything the hardware cannot send or receive over MIDI.

| Product | Header | Messages |
|---|---|---|
| Osirus / OsTIrus | Access `f0 00 20 33 01 10`, command `09` | front panel state once per block: MIDI activity per part, LFO and logo phases (raw floats) |
| Vavra | Waldorf `f0 3e 10 00`, commands `50`-`54` | LCD, LEDs, buttons, encoders, CGRAM; answers requests and pushes changes |
| Xenia | Waldorf `f0 3e 0e`, commands `60`-`62` | LCD, LEDs, buttons |
| NodalRed2x | Clavia `f0 33 0f 04`, types `5a`-`5d` | set and get pot positions, part CC, master tune |
| JE8086 | `f0 7d <cmd> ... f7` (non-commercial id) | LCD CGRAM and DDRAM, button, LEDs, SetParam for emulator parameters such as master volume |

Recipe, JE8086 being the cleanest implementation:

1. Subclass `synthLib::SysexRemoteControl` in the device library: static encoders
   (`createSysexHeader`, `sendSysex...`) plus `receive()` overloads that fire `baseLib::Event`s.
   The controller uses the same class to decode.
2. In the device's `sendMidi`, give the codec the first look, before the state cache and the
   firmware: `if (m_sysexRemote.receive(...)) return true;`.
3. Device→editor messages use source `MidiEventSource::Internal`, which routes to the editor
   only. Push them when something changed after `processAudio`, **and** answer explicit request
   messages so that an editor opened later, a state load or a bridge reconnect can resync
   (Vavra's front panel requests LCD and LEDs in its constructor, NodalRed2x requests pot
   positions in `onStateLoaded`).
4. Keep payloads 7-bit clean (nibbles) and validate exact lengths. Raw floats or 8-bit masks
   work in process and over the bridge but break once routed to a physical port.
5. Id: an unused command range under the real manufacturer header, or `0x7d`. **Never `0x60`**:
   `synthLib::MidiTranslator` claims `f0 60` and swallows it.
6. Persist emulator-only state by emitting these messages from `getState`.
7. A firmware that polls a key matrix only sees a button that stays down for a while; send press
   and release as two messages with a real delay (MCP `click_element` holds 80 ms for this).

---

## 7. DSP bridge

User documentation: website `docs/dsp-bridge.md`. Code: `source/framework/tools/bridge/`
(`bridgeLib`, `client`, `server`) and `source/framework/networkLib/`.

How it works:

- Discovery by UDP broadcast to port 56303 (`PInf`), answered with `SInf`; the session runs over
  TCP port 56362.
- The client sends `PInf` and `DCrP` (device create parameters without ROM data). The server
  looks the ROM up by MD5 in its cache; if it is missing it replies `ROMr`, the client resends
  with data and the server stores it. The server never searches for ROMs itself.
- The server picks the server plugin whose `{pluginVersion, plugin4CC, pluginName,
  protocolVersion}` matches exactly: client and server plugin must come from the same version.
- At runtime: one `MIDI` per event, `Wave` per audio block (the client's audio thread waits for
  the output, up to 10 s), `RqDS` / `DvSt` for state, `SmpR` sample rate, `DspC` clock, `UnkD`
  unknown custom data; every change is answered with `DevI` (device info). The server pushes the
  device state periodically and keeps it per session for reconnects.
- The server loads `<server executable dir>/plugins/*` libraries that export
  `bridgeDeviceCreate`, `bridgeDeviceDestroy` and `bridgeDeviceGetDesc`.

What a product adds:

1. `serverPlugin.cpp` in the plugin directory. `juce.cmake` requires it, compiles it into the
   plugin and into the `<Product>ServerPlugin` shared library:
   ```cpp
   // ReSharper disable once CppUnusedIncludeDirective
   #include "client/plugin.h"

   #include "<x>Lib/<x>device.h"

   synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
   {
   	return new <ns>::Device(_params);
   }
   ```
2. `Processor::getRemoteDeviceParams` filling `romData`, `romName`, `customData`.
3. A device that needs nothing but its parameters.

Nothing else: no registry, no server CMake change. `juce.cmake` builds `<Product>ServerPlugin`
into the server's `plugins` folder and installs it into component `DSPBridgeServer`; an FX
variant gets its own server plugin.

Not transported, plan around it: `DeviceCreateParams::homePath`; `SMidiEvent::type`,
`transportGeneration` and `port` (transport markers stop at the client);
`getDynamicSamplerates`; `canModifyDspClock` (reports false remotely). The client ignores error
replies from the server.

Test remote mode for every new product: run `dsp56300EmuServer` locally, set
`<VALUE name="supportDspBridge" val="1"/>` in `<Documents>/The Usual Suspects/<Product>/config/<Product>.xml`,
right-click the editor, Device Type, pick the server. Editing, patch manager, LCD and state
save/restore must behave exactly as in local mode. Anything that works only locally broke the
MIDI rule.

---

## 8. Plugin processor

Chain: product processor → `jucePluginEditorLib::Processor` → `pluginLib::Processor` →
`juce::AudioProcessor`.

| Member | Notes |
|---|---|
| constructor | `BusesProperties` matching the device channels (NodalRed2x: outputs only; JE8086: stereo out plus stereo in), config options, `pluginLib::initProcessorProperties()` (reads the `JucePlugin_*` defines, `Plugin4CC` and `BinaryData`). Then `getController();` and `Processor::setLatencyBlocks(getConfig().getIntValue("latencyBlocks", ...))`, which boots the device during host instantiation (see `jePluginProcessor.cpp`) |
| `createDevice()` | pure; throw `DeviceException(FirmwareMissing)`; build from the same parameters as `getRemoteDeviceParams` |
| `getRemoteDeviceParams(DeviceCreateParams&)` | call the base (sample rates), then add ROM and `customData` |
| `createController()` | pure; `new <ns>::Controller(*this)`. Virus boots the device first because its controller depends on the ROM model |
| `createEditorState()` | pure, from `jucePluginEditorLib::Processor` |
| destructor | call `destroyEditorState()`; the base asserts it is gone |
| `saveChunkData` / `loadChunkData` | optional product chunks (ROM choice, voice expansion); always call the base |
| `processBpm` | optional |
| `createPluginFilter()` | the JUCE factory function |

The framework already saves: remote device selection, device state, output gain, DSP clock,
device sample rate, resampler mode, MIDI ports, skin variables, routing matrix, MIDI Learn,
program change banks, editor state, parameter links.

Per-product folders derive from the product name: `<Documents>/The Usual Suspects/<Product>/`
with `roms/`, `config/<Product>.xml`, `patchmanager/`, `skins/`. FX variants share the synth's
folder.

---

## 9. Controller

Subclass `pluginLib::Controller(processor, "parameterDescriptions_<x>.json")`. The JSON is read
from BinaryData; a file with that name next to the plugin binary overrides it, handy while
developing and a trap otherwise.

### 9.1 Members to implement

| Member | Do |
|---|---|
| constructor | `registerParams(processor, partFormatter)`, register listeners, send the initial requests (or call `onStateLoaded()`) |
| `sendParameterChange(const Parameter&, ParamValue, Origin)` | pure; turn one edit into MIDI |
| `parseSysexMessage(const SysEx&, MidiEventSource)` | pure; decode dumps and parameter changes |
| `onStateLoaded()` | pure; request everything again. Called after a DAW state restore, a device reboot and a remote fallback |
| `getPartCount()` | default 16; return the real number of multitimbral parts (§11) |
| `getPartsForMidiChannel(channel)` | default `{}`, which disables CC mapping via `controllerMap` and MIDI Learn auto-part; implement it |

Optional: `parseControllerMessage` (the default maps CCs through `controllerMap` for the parts
from `getPartsForMidiChannel`), `isDerivedParameter` (default true: a repeated `{page, index}`
becomes a mirrored sibling; return false for bitfields that share a byte), `setCurrentPart`.

### 9.2 Startup: request the complete state

Request global/system data, the mode, the multi or performance, and a single for every part.
If the firmware drops bursts, chain the part requests: ask for the next single when the previous
one arrived. Examples: `xtController` (global + mode, then multi, then singles),
`n2xController::onStateLoaded` (single edit buffer per part, performance, pot positions),
`jeController` (system + temporary performance), `VirusController` (total + arrangement).

### 9.3 Receiving

- Parse with packets: `parseMidiPacket(name, data, values, sysex)`. The overload taking
  `std::string& name` tries every packet and reports which one matched, so every packet needs
  distinguishing constant bytes.
- Apply a whole dump with `applyPatchParameters(values, part)`; it uses `Origin::PresetChange`
  and refreshes the host display. For single values use `findSynthParam(part, page, index)` and
  `setValueFromSynth(value, origin)` on the result.
- Echo suppression works by value: `setValueFromSynth` records the value, so the parameter's
  listener sees no change and nothing goes back to the device. Writing the `juce::Value`
  directly **does** transmit.
- Parameters with class `Global` or `NonPartSensitive` exist once, on part 0; look them up there.

### 9.4 Sending

- One edit → one message where the device has a parameter change message, for example
  `sendSysEx("parameterchange", {{MidiDataType::Part, part}, ...})`, or a CC.
- Several parameters in one byte: `combineParameterChange(result, "singledump", param, value)`
  packs that byte from the current values.
- No per-parameter message, or the firmware applies the value only on a full load: send the
  whole dump, rate limited with `Parameter::setRateLimitMilliseconds`.
- After sending a whole patch: patch buffer, location and device id bytes, call
  `MidiPacket::updateChecksums`, send, `sendLockedParameters(part)`, then request the patch back
  so the editor shows what the firmware accepted.
- Locked parameters are only flagged by the framework; the controller has to keep them
  (NodalRed2x merges locked values into the dump before sending, Virus sends a corrected dump).
- A packet needs every field it declares. A missing `MidiDataType::DeviceId` makes
  `MidiPacket::create` fail: assert in Debug, **silently dropped in Release**.

### 9.5 Parameter objects

`pluginLib::Parameter::Origin` is one of Unknown, PresetChange, Midi, HostAutomation, Ui,
Derived. The host parameter id is `page_part_index` (plus `_uid` for duplicates), grouped per
part through the part formatter. Append new parameters at the end of the JSON with
`"version": N` so VST2 and AU automation indices in old projects stay valid.

---

## 10. Parameter descriptions and midi packets

File `parameterDescriptions_<x>.json`, parser `jucePluginLib/parameterdescriptions.cpp`,
packets `jucePluginLib/midipacket.*`. The JSON is BinaryData: rebuild after every change.

### 10.1 Top-level keys

| Key | Notes |
|---|---|
| `parameterdescriptiondefaults` | fallback for every per-parameter key |
| `valuelists` | **required**. Without it the file yields zero parameters and almost no error. Each list is an array of strings, or an object `{"<int>": "text"}` |
| `parameterdescriptions` | array of parameters |
| `midipackets` | may be `{}` |
| `regions` | `{"id": ..., "name": ..., "parameters": [...]}` and/or `"regions": [ids defined earlier]`; used by lock, link, copy and paste |
| `controllerMap` | `{"cc": 7, "param": "Gain"}` or `"pp"`; several parameters may share a CC; NRPN entries do not work |

`parameterlinks` is not parsed; unknown keys are ignored. `//` and `/* */` comments are removed
by plain text search, so never write `//` inside a string.

### 10.2 Per parameter

`name` (unique), `displayName`, `min`, `max`, `default`, `toText` (value list name), `isPublic`,
`isDiscrete`, `isBool`, `isBipolar`, `step`, `page` (0-255), `index`, `class` (`Global`,
`NonPartSensitive`, `MultiOrSingle`, combined with `|`), `version`, `softknobTargetSelect` with
`softknobTargetList`.

- Identity is `{page, part, index}`. Names are unique, indices may repeat; a repeat silently
  becomes a derived, linked parameter. Give copies distinct pages.
- **An `index` of 128 or more is folded:** `page += index / 128`, `index %= 128`. Code comparing
  `page == N` misses the folded parameters.
- JSON order is the description index. Positional C++ tables must follow it: append, never
  insert.
- A value list shorter than `max - min + 1` logs an error but keeps the parameter.

### 10.3 The packet DSL

A packet is an array of definitions. Every element except `param` takes one byte.

| `type` | Fields | Meaning |
|---|---|---|
| `byte` | `value` (hex string) | constant, must match when parsing |
| `param` | `name`, `mask` (read as **hex**, default `ff`), `shift`, `shiftL`, `part` (0-15) | packed as `((v & mask) << shift) >> shiftL`, unpacked in reverse. Consecutive params share a byte while their masks do not overlap; repeating the same name starts the next byte (values wider than one byte) |
| `checksum` | `first`, `last` (inclusive byte indices), `init` | `(init + sum) & 0x7f` |
| `deviceid`, `bank`, `program`, `page`, `part`, `paramindex`, `paramvalue` | - | one byte supplied or extracted through `MidiPacket::Data` (`MidiDataType`) |
| `null` | - | writes 0, ignored when parsing |

From the tree:

```json
"parameterchange": [
	{"type": "byte", "value": "f0"},
	{"type": "byte", "value": "00"},
	{"type": "byte", "value": "20"},
	{"type": "byte", "value": "33"},
	{"type": "byte", "value": "01"},
	{"type": "deviceid"},
	{"type": "page"},
	{"type": "part"},
	{"type": "paramindex"},
	{"type": "paramvalue"},
	{"type": "byte", "value": "f7"}
]
```
(`parameterDescriptions_C.json`)

```json
{"type": "param", "name": "O2Pitch", "mask": "f"}, {"type": "param", "name": "O2Pitch", "shiftL": 4},
```
An 8-bit value sent as two nibbles, low nibble first (`parameterDescriptions_n2x.json`).

```json
{"type": "param", "name": "ArpUserPattern1", "mask": 1, "shift": 3},
{"type": "param", "name": "ArpUserPattern2", "mask": 1, "shift": 2},
{"type": "param", "name": "ArpUserPattern3", "mask": 1, "shift": 1},
{"type": "param", "name": "ArpUserPattern4", "mask": 1, "shift": 0},
```
Four flags in one byte (`parameterDescriptions_xt.json`).

### 10.4 Using packets from C++

- `sendSysEx(packetName, data)` fills parameter bytes with part 0 values;
  `createMidiDataFromPacket(sysex, packetName, data, part)` with the values of one part.
- `parseMidiPacket(...)`: see §9.3. Checksum mismatches are ignored when parsing by default;
  the size has to match exactly.
- `combineParameterChange` returns the raw value when the byte holds one definition and the
  packed byte when it is shared.
- `MidiPacket::updateChecksums(sysex)` (exact size required), `getByteIndexForType`,
  `getDefinitionByParameterName`.

---

## 11. Parts

- **A part is one multitimbral slot**: a timbre of a multi or performance, addressed through its
  own MIDI channel. Oscillators, sources, layers or envelopes of one patch are not parts; they get
  their own parameter names (`Osc1Shape`, `Osc2Shape`). In single mode the patch is part 0.
- At most 16 parts: parameter arrays, lock regions, patch manager state, the RML data models
  `part0` to `part15`, the program change router and the packet `part` field all stop there.
- On `oss/main`: Virus 16 (Snow 4), Vavra 16, Xenia 8, NodalRed2x 4 (performance slots A-D),
  JE8086 2 (Upper and Lower, each on its own MIDI channel). JE8086 adds a pseudo part 2
  ("Performance") that only part buttons and patch requests use; do not copy that.
- `Global` / `NonPartSensitive` parameters exist once, on part 0. Waldorf's per-instrument multi
  settings are separate named global parameters (`MI0MidiChannel`, ...), not part parameters.
- Wiring: the base `jucePluginEditorLib::Editor` subscribes to `Controller::onCurrentPartChanged`
  and calls the virtual `Editor::onCurrentPartChanged(part)`, which updates the patch manager and
  the `currentPart` data model. Part buttons call `Editor::setCurrentPart`; override
  `onCurrentPartChanged` to refresh the product's own part highlight or LCD, and call the base.
  UI, MIDI and the MCP `set_current_part` tool then update the editor alike.
- `getPartsForMidiChannel` maps incoming MIDI to parts, for multi mode (per-part channel
  parameters) and for single mode.
- The program change router uses the MIDI channel number as the part index.

---

## 12. Patch manager

Subclass `jucePluginEditorLib::patchManager::PatchManager` (itself a `pluginLib::patchDB::DB`)
and create it in `Editor::createPatchManager`. That is only called when the skin contains an
element with id `patchmanager`.

| Pure virtual | Do |
|---|---|
| `requestPatchForPart(Data&, part, userData)` | the sysex of a part's current patch, usually from the controller's last received dump; `userData` lets the save menu ask for a multi or arrangement |
| `loadRomData(results, bank, program)` | factory presets from the ROM, or return false |
| `initializePatch(Data&&, defaultName)` | create the `Patch`: name, tags, bank, program, and **`hash`**. `Patch::setHashFromMessages(headerSize, footerSize)` hashes the payload of every message without header (device id, dump location) and footer (checksum), so a sound hashes the same wherever it is stored (see `mqPatchManager`, `xtPatchManager`). The database creates every patch through `DB::createPatch`, which asserts in Debug builds that the hash is set |
| `applyModifications(patch, fileType, exportType)` | write name, program and tags into the sysex, fix checksums; export types Clipboard, DragAndDrop, EmuHardware (strip emulator-only additions), File |
| `getCurrentPart()` | the controller's current part |
| `activatePatch(patch, part)` | send the patch to that part's edit buffer through the controller, keep locked parameters, read back |

Optional: `parseFileData` (the default reads `.syx` and SMF `.mid`; add third-party formats and
bank splitting there), `equals`.

Constructor: tag type names, group types, `startLoaderThread()`; destructor:
`stopLoaderThread()`. A product that passes its own group type list must add new group types
when the framework gains them (the MIDI Banks group went missing that way once).

ROM data sources only if the ROM contains factory presets (Virus, JE8086): add them as
`SourceType::Rom` with a unique `midiBankNumber` per bank so program change routing works. User
reassignments persist in `patchmanagerdb.json` and win over the defaults.

---

## 13. Editor and skin

[skin_authoring.md](skin_authoring.md) is the deep dive. The minimum:

- `PluginEditorState` subclass: the constructor passes `g_includedSkins` from the generated
  `skins.h` and calls `loadDefaultSkin()`; implement `createEditor(const Skin&)`.
- `Editor` subclass: `getDemoRestrictionText()` (pure, `{}` is fine), `create()` calling the base
  first, `createPatchManager()`. Optional: `onCurrentPartChanged` (refresh the product's own part
  highlight, call the base, see §11), `initPluginDataModel` (extra data model keys, added
  before the model handle is fetched) and `createDeviceSpecificSettings` (template
  `tus_settings_gui_<Product>.rml` or `tus_settings_dspaudio_<Product>.rml`, listed in `ASSETS`).
- In `create()` wire the helpers the skin provides by element id: `FocusedParameter`
  (`FocusedParameterName` / `Value` / `Tooltip`), `MidiPorts` (`MidiIn`, `MidiOut`), part
  buttons (`PartButton` subclass), LCD (`jucePluginEditorLib::Lcd` subclass fed by custom
  sysex), LEDs (`Led`), preset previous/next/save, ROM selector, version labels.
- Root RML: link `tus_default.rcss` (plus `tus_juceskin.rcss` for `jucePos` layouts) and the skin
  RCSS in `<head>`; `<body>` with explicit size and `rootScale`; every `param="<Name>"` element
  inside a `data-model` ancestor (`partCurrent`, `partN`, `part0` for globals), because without
  one it never binds and nothing warns; `<template src="patchmanager"/>` inside a tab page; tabs
  through `tabgroup` / `tabpage` / `tabbutton`. Ranges and combo entries come from the parameter
  descriptions, never from RML.
- `addSkin("<Product>" "<skinName>" "skins/<dir>" "<root>.rml")` collects only the top level of
  the folder (`png rml rcss ttf lua svg`) at configure time: new files need a CMake reconfigure.
  The first `addSkin` is the default skin. Name the skin like its root `.rml`.
- Resources are looked up by basename across the skin, the product assets and
  `jucePluginData`: keep basenames unique, or a product file silently replaces a framework file.
- `buildSkinHeader()` writes `skins.h` into the source directory: add it to the plugin
  directory's `.gitignore`.
- Firmware-driven front panel: buttons and encoders send custom sysex (Vavra
  `mqFrontPanel`, JE8086 `btPerfPatch`); displays subclass `Lcd`.
- Free from the framework: settings dialog (skin, GUI scale, MIDI ports and routing matrix,
  MIDI Learn, latency, DSP clock, gain, resampler, DSP bridge), global context menu,
  per-parameter context menu (lock, link, copy, paste, MIDI Learn), parameter overlays, patch
  manager UI, drag and drop onto part buttons, editor state persistence, MCP server. A product
  cannot add a settings page, only a device-specific section on the GUI and DSP/Audio pages.
- Artwork masters for the shipped skins live in a separate private art repository, not here.

---

## 14. Build, packaging, changelog

### 14.1 `source/CMakeLists.txt`

- `option(${CMAKE_PROJECT_NAME}_SYNTH_<X> "Build <Product>" on)` next to the others. Option names
  are the one accepted place for a device name on `oss/main` (restructure_plan.md §9).
- `add_subdirectory(<maker>)` or the maker's CMakeLists gating the family. Plugin folders must
  come after `include(cmake/juce.cmake)`, behind `BUILD_JUCEPLUGIN`, and after
  `framework/tools/bridge`, whose `bridgeServer` and `bridgeClient` targets `createJucePlugin`
  uses.

### 14.2 Plugin `CMakeLists.txt`

The NodalRed2x file, generalised:

```cmake
cmake_minimum_required(VERSION 3.15)

project(<x>JucePlugin VERSION ${CMAKE_PROJECT_VERSION})

set(SOURCES
	<x>Controller.cpp <x>Controller.h
	<x>Editor.cpp <x>Editor.h
	<x>PatchManager.cpp <x>PatchManager.h
	<x>PluginEditorState.cpp <x>PluginEditorState.h
	<x>PluginProcessor.cpp <x>PluginProcessor.h
	parameterDescriptions_<x>.json
	skins/<skin>/<skin>.rml
	skins/<skin>/<skin>.rcss
)

SET(ASSETS "parameterDescriptions_<x>.json")    # plus tus_settings_*_<Product>.rml, if any

addSkin("<Product>" "<skin>" "skins/<skin>" "<skin>.rml")

buildSkinHeader()

juce_add_binary_data(<x>JucePlugin_BinaryData SOURCES ${ASSETS} ${ASSETS_SKINS})

createJucePlugin(<x>JucePlugin "<Product>" TRUE "<4CC>" <x>JucePlugin_BinaryData <x>Lib)
# synth + FX: createJucePluginWithFX(<x>JucePlugin "<Product>" "<4CC>" "<FX 4CC>" <x>JucePlugin_BinaryData <x>Lib)
```

Next to it: `serverPlugin.cpp` (§7) and `.gitignore` containing `skins.h`. `createJucePlugin`
reads `${SOURCES}` from the calling scope. Do not pass `NAMESPACE` to `juce_add_binary_data`,
`initProcessorProperties` expects `BinaryData::`.

### 14.3 What `createJucePlugin` provides

Company "The Usual Suspects", manufacturer `TusP`, bundle id `com.theusualsuspects.<Product>`,
LV2 URI `http://theusualsuspects.lv2/<Product>`, CLAP id `com.theusualsuspects.<4CC>`, the
defines `PluginName`, `Plugin4CC`, `PluginVersionMajor/Minor/Patch`, the macOS quarantine script
`macsetup_<Product>.command`, install components `<Product>-VST2/-VST3/-CLAP/-LV2/-AU`,
`pluginTester` ctests (VST2, VST3, LV2, AU for synths), AU validation on macOS (synths only; it
needs the packed AU zip, so run Pack before ctest), the packaged `changelog_<Product>.txt`
(optional), `tus_exportTarget` (synths only, this is what makes a product deployable), and
`<Product>ServerPlugin`.

Variants:

- **Synth + FX** (`createJucePluginWithFX`): `<Product>FX` is built only with
  `gearmulator_BUILD_FX_PLUGIN`. It has its own 4CC, components, server plugin and tests, but no
  `tus_exportTarget` (it deploys only because the rclone filter `*<Product>*` matches its
  archives), no AU validation, and shares the synth's data folder.
- **FX-only product:** `createJucePlugin(... FALSE ...)` plus an explicit
  `tus_exportTarget(<target>)`, otherwise it is never deployed.
- **Standalone-only product:** `source/ronaldo/88emu/88emuplayer/CMakeLists.txt` is the
  template (install component, `TUS_PRODUCT_NAME`, `tus_exportTarget`, changelog install by
  hand). CI builds Standalone only when `gearmulator_BUILD_JUCEPLUGIN_Standalone` is passed.

### 14.4 Packaging

One CPack component per product and format; archive names
`TheUsualSuspects-<Component>-<version>-<win64|MacOS|Linux_x86_64|Linux_aarch64>.zip`, plus
`.deb` and `.rpm` on Linux. `DSPBridgeServer` contains the server and every enabled
`<Product>ServerPlugin`. No README or license file is packaged, and there is no signing tooling
in the repository.

### 14.5 Linux distribution packages (OBS)

`installer/obs/` is the openSUSE Build Service package `home:theusualsuspects/TheUsualSuspects`.
It builds public `main` for openSUSE Tumbleweed and Leap, Fedora, Debian and Ubuntu, on x86_64
and on aarch64 where OBS offers it, and publishes installable repositories. One build per target
produces one package per product, `theusualsuspects-<lowercase product>`, holding that product's
VST2, VST3, CLAP and LV2 plugins. `installer/obs/README.md` has the mechanics, the target list
and the version bump.

A new product is four edits. The package name is lowercase, the paths inside keep the product
name:

| File | Add |
|---|---|
| `TheUsualSuspects.spec` | `%package -n theusualsuspects-<lower>` with `Summary:` and `%description`, and a `%files` block: `%license LICENSE.md`, the four `%dir` entries, then `%{_prefix}/lib/vst/<Product>.so`, `vst3/<Product>.vst3`, `clap/<Product>.clap`, `lv2/<Product>.lv2` |
| `debian.control` | a `Package: theusualsuspects-<lower>` stanza: `Architecture: amd64 arm64`, `Depends: ${shlibs:Depends}, ${misc:Depends}`, description |
| `debian.theusualsuspects-<lower>.install` | the same four paths as `usr/lib/...`, one per line |
| `TheUsualSuspects.dsc` | the package in the `Binary:` list |

Traps:

- **Public products only.** OBS clones `main` from the public GitHub repository, so a product
  that is not public yet neither builds there nor belongs in these files (name gate,
  restructure_plan.md §9).
- Debian package names must be lowercase, while the plugin files keep the product's case. Those
  file names are what `%files` and the `.install` file match: a rename breaks both silently.
- `%install` and `debian.rules` keep only the four plugin directories and delete everything else
  the tree installs, the test console and the bridge server plugin included. A product that ships
  more than plugins needs a rule there, not just a `%files` entry.
- Standalone-only products are not built at all: the OBS build passes
  `gearmulator_BUILD_JUCEPLUGIN_Standalone=OFF`.
- Nothing in the OBS project itself is per-product; its repositories are per distribution.

### 14.6 Changelog, `doc/changelog.txt`

- The section header `<Product>:` must equal the product name, case-sensitive. Entries shared by
  products use slash headers (`Vavra/Xenia:`); `Framework`, `DSP` and `Patch Manager` are global
  and go into every product's file.
- The generator treats **any** line starting with an uppercase letter or digit and ending in `:`
  as a section header, and a leading dotted number as a version: watch continuation lines.
- A product without a section ships without a changelog, silently. Add its section under the
  current version when the product is added.
- GitHub release notes need a `<version>:` header equal to `project(gearmulator VERSION ...)`.
- Entries `- [New]`, `- [Imp]`, `- [Fix]`, continuation lines indented 8 spaces (`CLAUDE.md`).

### 14.7 Portability traps every new product has hit

- Path casing in `#include` and CMake: Windows and macOS forgive it, Linux CI does not
  (`addSkin` stops configure on a case mismatch).
- Missing standard headers (`<cstdint>`, `<cstring>`, `<cstddef>`, `<chrono>`) compile with MSVC
  and fail with newer GCC.
- Windows-only helper tools need an `if(WIN32)` guard and committed output.
- The VST3, LV2 and AU manifest helpers instantiate the plugin during the build: construction
  must survive without a ROM and must not block.
- A new shipped executable or shared library: check its glibc and GLIBCXX floor with
  `objdump -T` on Linux.

---

## 15. CI and branches

### 15.1 Where the work happens

- A private device branch in its own worktree, pushed to `private_gearmulator` (`CLAUDE.md`,
  Git Conventions). Merge `oss/main` regularly.
- Submodule commits (`dsp56300`, `mc68k`) go on the submodule's real branch and are pushed to its
  GitHub repository **before** the superproject commit that points at them. A pushed gitlink to
  an unpublished submodule commit breaks checkout for every Jenkins build, whatever branch it
  builds. Those submodule repositories are public: no unreleased names in their branch names or
  commit messages.

### 15.2 `scripts/products.cmake`, required

Add `gearmulator_SYNTH_<X>` to `products`. `scripts/generate.cmake` forwards only list members to
the real configure: an unlisted `-Dgearmulator_SYNTH_<X>=on` or `=off` is **silently dropped**,
the option keeps its default (usually `on`) or a stale cache value, and CI can no longer switch
it. A listed product that is not passed is forced `off`. Values must be lowercase `on` / `off`.

### 15.3 Jenkins

- `scripts/JenkinsfileMulti` mirrors the inline pipeline of job `dsp56300_main_multi`. Three
  edits:
  1. `booleanParam(name: 'Synth<X>', defaultValue: true, description: '')`
  2. `if(params.Synth<X>) env.childDisplayName += " S<x>"`
  3. `" -Dgearmulator_SYNTH_<X>=" + (params.Synth<X> ? "on" : "off") +` in `env.synths`, keeping
     the `+` at the end of every line but the last.
- Apply the same three edits to the **live** job: GET its `config.xml`, edit that copy, POST it
  back. The live job is ahead of every branch's mirror, so uploading one branch's file would drop
  toggles of other branches. A new parameter shows in the build form after the first run.
- `scripts/Jenkinsfile` (job `dsp56300_main`) is product-agnostic. Build a private branch with
  `CustomBranch`, `UploadFolder=internal`.

### 15.4 GitHub Actions

- Private self-hosted runners (`.github/workflows/private-build.yml`, only on the private
  remote): `synths: auto` builds the `products.cmake` entries that are new compared to main, so
  `products.cmake` is the only edit needed. `upload_folder: auto` means the release folder on main
  and `internal` elsewhere.
- Public hosted workflows: `cmake.yml` and `nightly.yml` build the option defaults; `release.yml`
  uses the `CMakePresets.json` presets `github-base` and `zynthian`, which list products
  explicitly (unlisted products get the option default). Decide ON or OFF there when the product
  goes public.
- Both CI systems post build results to Discord `#builds` automatically.

---

## 16. Infrastructure outside the repository

### 16.1 Deploy folder, `https://dsp56300.com/builds/<slug>/<tier>/`

Deploy and Upload copy the archives with rclone to `builds/<slug>/<tier>/`
(`scripts/deployAll.cmake`: slug = lowercase product name, tiers from `FOLDER`, several joined
with `+`, for example `internal+donators`). **Prepare the folder before the first Deploy or
Upload:** rclone creates missing folders without protection, and the new name would show up in
the public index. Host, logins and passwords are in the user's memory (FTP deploy note), never in
the repository; if an FTP/Release session is running, coordinate with it.

1. Download the current root `builds/.htaccess` (several people edit it), add the slug to
   `IndexIgnore`, and upload it **first**.
2. Create `builds/<slug>/.htaccess` that whitelists every tier the product uses and 404s the rest:
   ```
   RewriteEngine On
   RewriteRule ^(internal|donators)(/.*)?$ - [L]
   RewriteRule ^.*$ - [R=404,L]
   ```
   A tier missing from the regex returns 404 even with files in it.
3. For each tier: `<tier>/.htaccess`, copied from an existing product with `AuthUserFile` pointed
   at the new slug's `.htpasswd` (AuthName "Protected" for internal, "Donators Only" for donators),
   and `<tier>/.htpasswd` (internal: the shared file; donators: one line, user = slug, password
   hashed with `openssl passwd -apr1`). **Protect `donators` on day one**, even while empty.
4. All files LF only, transferred in binary mode. Tier `.htaccess` and `.htpasswd` without a
   trailing newline, product and root `.htaccess` with one.
5. Verify over HTTPS: the product folder answers 404, each tier 401 without credentials, 200 with
   them and 401 with a wrong password; `https://dsp56300.com/builds/` does not list the slug.
   Download every uploaded file back and compare.

Operations:

- FTP has no server-side copy: moving builds between tiers is download plus upload. Copying
  internal to donators is manual unless the build used `internal+donators`. Mirror the file set
  the previous donators version had.
- Jenkins never deletes. Old versions are removed by hand on request, by explicit path, after
  checking that the new version has the same set of files.
- A rename moves the folder: remove the old one, create and protect the new one, update
  `IndexIgnore`. Branches still using the old name deploy into a folder that no longer exists.

### 16.2 Discord

- A thread `<Name> Updates` in the team's internal developer channel (channel and thread ids are
  in the changelog posting skill named below). The thread title may use the hardware name; the
  posts use the product name. No MCP tool creates threads: ask the user to create it.
- First post, a plain message:
  ```
  **<Product> Download Links**
  donators: https://dsp56300.com/builds/<slug>/donators/
  internal: https://dsp56300.com/builds/<slug>/internal/

  Credentials for donators: **only share with donators**
  user: ``<slug>``
  pass: ``<donators password>``

  Credentials for internal: **do not share with anyone!!**
  user: ``<internal user>``
  pass: ``<internal password>``
  ```
- Add the thread id and the product's section header to the changelog posting skill,
  `.claude/skills/tus-post-changelog/SKILL.md` (gitignored; update the main worktree's copy).
- Changelog posts are code blocks under Discord's 2000 character limit, split at entry boundaries
  and sent one after another. Dormant threads are archived automatically; the bot then gets
  "Channel not found" and the user has to reopen them.

### 16.3 YouTrack

Add the product name to both enum fields. The MCP tools cannot change bundles; use the REST API
with the `YouTrackTUS` token from `.mcp.json` (base `https://tus.youtrack.cloud/api`), as for
versions in `CLAUDE.md`:

- BUG (project `0-4`), field **Emulator**, enum bundle `155-7`:
  `POST /admin/customFieldSettings/bundles/enum/155-7/values` with `{"name":"<Product>"}`
- EMU (project `0-3`), field **Product**, enum bundle `155-4`:
  `POST /admin/customFieldSettings/bundles/enum/155-4/values` with `{"name":"<Product>"}`

Confirm with `get_issue_fields_schema`.

### 16.4 When the product goes public

1. Name gate on everything that lands on `oss/main`, commit messages included (restructure_plan.md §9).
2. `README.md` device list and option table; `CLAUDE.md` per-synth table, test console list and
   CMake flags; `CMakePresets.json` presets; `installer/obs` packaging (§14.5).
3. Remove the slug from the root `IndexIgnore`; the tiers stay protected.
4. Release per `CLAUDE.md` "Release Workflow"; Jenkins with `GitHub=true` runs
   `scripts/deployGitHub.cmake` (draft release, notes from `doc/changelog_split/<version>.txt`).
5. Website `../dsp56300.github.io`:
   - `downloads/<slug>.md` modelled on `downloads/je8086.md` (performance test links with
     `product=<Product>TestConsole`, plugin links with `product=<Product>`)
   - navigation entries under Downloads and Technical in `_config.yml`
   - `technical/<slug>.md`, a section in `docs/faqs.md`, links in `docs/installation-guide.md`
     and `docs/patch-manager.md`
   - after the GitHub release is published: `builds/<version>.json` and `builds/versions.json`
     (skill `tus-update-website`)
   - the download page derives product, format and OS by splitting asset names on `-`, which is
     why product names must not contain one
6. Plugin Manager catalog `installer/plugins.json` on the website (`id`, `name`, `description`,
   `thumbnail`, `image`, `testConsole`, `fxFormats`, `versions`), read by the installer tool on
   branch `feature/installer`.
7. Discord: post the changelog to the public products' threads only.

---

## 17. Verification

1. The test console boots, renders audio and reaches an acceptable real-time percentage.
2. `pluginTester` passes with the ROM findable (`<Documents>/The Usual Suspects/<Product>/roms/`
   or next to the plugin binary). It also passes with zero parameters, so check the debugger
   output for `ParameterDescription` errors (run it under cdb).
3. In a host with the MCP server enabled ([mcp_server.md](mcp_server.md)): set a **different**
   value in every part and read each back, load and save presets, check LCD, LED and button round
   trips, do a `get_plugin_state` → reset → `set_plugin_state` round trip, take screenshots at
   several GUI scales.
4. DAW: save a project, reopen, compare; the same with the FX variant.
5. DSP bridge: repeat 3 and 4 in remote mode (§7).
6. CI: Windows, macOS (Xcode, AU validation), Linux x86_64 and aarch64 (path casing), ctest.

---

## 18. Checklist

Code
- [ ] product name, slug, CMake option, 4CC (and FX 4CC), target prefix settled (§4)
- [ ] device library: `synthLib::Device` subclass built from `DeviceCreateParams`, throws `DeviceException`
- [ ] ROM loader detecting by content; `getRemoteDeviceParams` and `createDevice` share parameters
- [ ] custom sysex codec (`SysexRemoteControl`), source `Internal`, request messages for resync
- [ ] `getState` appends; emulator-only state persisted
- [ ] destructor unblocks all threads; boot has a timeout
- [ ] test console
- [ ] processor, controller, parameter JSON with `valuelists`, midi packets
- [ ] `getPartCount`, `getPartsForMidiChannel`, part wiring through `onCurrentPartChanged`
- [ ] patch manager setting `hash`; `activatePatch` keeps locks and reads back
- [ ] editor state, editor, skin (data models, `patchmanager` template), `.gitignore` with `skins.h`
- [ ] `serverPlugin.cpp`

Build and CI
- [ ] option and `add_subdirectory` (`source/CMakeLists.txt`, maker CMakeLists)
- [ ] `scripts/products.cmake`
- [ ] `scripts/JenkinsfileMulti` and the live `dsp56300_main_multi` pipeline
- [ ] `doc/changelog.txt` section `<Product>:`
- [ ] `installer/obs`: spec subpackage, `debian.control` stanza, `.install` file, dsc `Binary:` (public products)
- [ ] green on Windows, macOS, Linux x86_64 and aarch64, ctest included

Infrastructure
- [ ] deploy folder: `IndexIgnore` first, product rewrite, internal and donators protected, verified over HTTPS
- [ ] Discord thread, download links post, changelog skill entry
- [ ] YouTrack: BUG `Emulator` and EMU `Product` values

Going public
- [ ] name gate, README, CLAUDE.md, CMakePresets
- [ ] `IndexIgnore` removal, GitHub release, website pages, navigation and builds cache, installer catalog, Discord post
