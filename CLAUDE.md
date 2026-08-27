# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Gearmulator is a low-level IC emulator that recreates classic virtual analog synthesizers (Access Virus, Waldorf microQ/XT, Clavia Nord Lead 2x, Roland JP-8000) by emulating original DSP56300, MC68K and H8S processors and running authentic firmware ROMs as audio plugins (FST, VST3, AU, CLAP, LV2).

## Build Commands

**Current dev setup uses `temp/cmake_vs26` with Visual Studio 2026.**

```bash
# Configure (Windows)
cmake . -B temp/cmake_vs26 -G "Visual Studio 17 2022"

# Build (use Debug for quick compile checks, Release for full optimization)
cmake --build temp/cmake_vs26 --config Debug -j 4
cmake --build temp/cmake_vs26 --config Release -j 4

# Package
cd temp/cmake_vs26 && cpack -G ZIP

# Run tests
ctest -C Release
```

Per-synth CMake flags: `-Dgearmulator_SYNTH_OSIRUS=ON`, `_OSTIRUS`, `_VAVRA`, `_XENIA`, `_NODALRED2X`, `_JE8086`. Plugin format flags: `gearmulator_BUILD_JUCEPLUGIN`, `_VST2`, `_VST3`, `_CLAP`, `_LV2`, `_AU`, `_Standalone`, plus `gearmulator_BUILD_FX_PLUGIN`.

Convenience scripts: `build_win64.bat`, `build_linux.sh`, `build_mac.sh`.

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

**Emulation stack:** DSP56300 emulator (`source/cpu/dsp56300/`, JIT via asmjit) + MC68K emulator (`source/cpu/mc68k/`, Musashi) + H8S core (`source/cpu/h8s/`) form the core. Each synth has a device library that loads firmware ROMs, initializes processor memory, and handles MIDI/audio via HDI08.

**Per-synth pattern:**
| Emulator | Hardware | Device Lib | Plugin Dir |
|---|---|---|---|
| Osirus | Virus A/B/C | `axel/virusLib/` | `axel/osirusJucePlugin/` |
| OsTIrus | Virus TI/TI2/Snow | `axel/virusLib/` | `axel/osTIrusJucePlugin/` |
| Vavra | Waldorf microQ | `waldi/microq/mqLib/` | `waldi/microq/mqJucePlugin/` |
| Xenia | Waldorf MW II/XT | `waldi/xt/xtLib/` | `waldi/xt/xtJucePlugin/` |
| Nodal Red 2x | Nord Lead/Rack 2x | `claudia/n2x/n2xLib/` | `claudia/n2x/n2xJucePlugin/` |
| JE-8086 | Roland JP-8000 | `ronaldo/je8086/jeLib/` | `ronaldo/je8086/jeJucePlugin/` |

Shared per-manufacturer code: `waldi/common/wLib/` (microQ + XT), `ronaldo/common/` and `ronaldo/esp/` (Roland).

**Shared libraries** (all under `source/framework/`):
- `synthLib/` — Device base class, DAC, resampling, MIDI routing
- `juce/jucePluginLib/` — Parameter system, MIDI Learn, Patch Manager, program change routing
- `juce/jucePluginEditorLib/` — Plugin editor UI, parameter overlays, settings pages
- `juce/juceRmlUi/` — RmlUi integration (HTML/CSS-like UI framework)
- `baseLib/` — Filesystem, logging, events, binary streams
- `hardwareLib/` — LCD, buttons, encoders abstractions

**Plugin build flow:** `createJucePluginWithFX()` macro in `source/cmake/juce.cmake` → links device lib → Processor inherits `synthLib::Plugin` wrapping `synthLib::Device` → skins via RML/RCSS compiled into binary data.

## Code Conventions

- **Tabs for indentation** (tab size 4, UseTab: Always), 120 char column limit
- **Braces on new lines** for all constructs; namespace content indented
- **Naming:** PascalCase classes, camelCase functions/vars, `m_` member prefix, `_` parameter prefix (`void func(int _param)`)
- **Namespaces:** camelCase (`virusLib`, `synthLib`, `dsp56k`)
- **Early returns** preferred over deep nesting
- `.clang-format` in `source/` directory
- C++17 required

## Git Conventions

- Do NOT include `Co-authored-by` trailers in commit messages
- Do NOT commit without explicit user approval
- Git remotes: `gearmulator` (public OSS), `private` (development), also `nas`, `codeberg`, `EvilDragon`
- DSP submodule (`source/cpu/dsp56300/`) is also owned by user — changes there are fine
- The device branches are **worktrees of this repository**, so they share one `.git` and one set of remote-tracking refs. A push from any session moves the shared ref: check `git ls-remote` rather than assuming something is unpushed.
- `oss/main` is the public remote. Unreleased devices must not be named in anything that lands there; private branches need no such care. See §9 of `doc/restructure_plan.md`.

## Key Build Files

- `source/cmake/base.cmake` — Compiler flags, platform-specific optimization settings
- `source/cmake/juce.cmake` — JUCE plugin configuration and multi-format support
- `source/cmake/skins.cmake` — Skin asset compilation
- `source/3rdparty/README.md` — which third-party code is forked, why, and how to update it
- `scripts/Jenkinsfile` / `JenkinsfileMulti` — Private CI (Jenkins)
- `.github/workflows/cmake.yml` — Public CI (GitHub Actions)

## Where to Make Changes

1. **Device-level changes** → the device lib (e.g., `source/axel/virusLib/device.cpp`)
2. **Plugin UI** → the plugin dir (RML/RCSS for layout, processor for logic)
3. **Shared plugin infra** → `source/framework/juce/jucePluginLib/` or `source/framework/synthLib/`
4. **DSP emulator** → `source/cpu/dsp56300/source/dsp56kEmu/`
5. **New parameter** → update `parameterDescriptions_*.json`, map MIDI in processor, update skin RML

## Critical Implementation Details

- **State save/restore:** Device `getState()` MUST append to `_state` vector with `insert()`, never `assign()` — the Plugin layer prepends version headers
- **RmlUi threading:** DOM modifications MUST happen on JUCE message thread. Use `juce::MessageManager::callAsync` from audio/MIDI callbacks
- **callAsync safety:** Use static instance-set pattern to guard lambdas against use-after-free
- **Voice expansion (Xenia/Vavra):** Multiple DSP56300 instances connected via ESSI1 ring bus; main DSP is last (`g_mainDspIdx = g_dspCount - 1`)
- **Includes arrive by declaration, not by luck:** there is no blanket `source/` include root any more. If a header stops resolving after a move, decide whether the dependency is real — repath it — or was never real, and delete the include. Do not add a link edge to make it compile.

## Detailed Reference

See `.github/copilot-instructions.md` for comprehensive documentation on MIDI Learn, Patch Manager, program change routing, Jenkins CI details, YouTrack workflow, release process, and voice expansion internals. Note that its path references predate the source tree restructure.
