# Third-party code

Everything in this directory was written by somebody else. The rule is the same
for all of it regardless of tier: **do not edit these files in place.** Patch it
in our fork and bump the submodule, or — for the vendored copies — understand
that you are creating a permanent local divergence and say so in the commit.

Two tiers, distinguished by how the code is stored rather than by folder:

## Submodules

Tracked in `.gitmodules`, updated with `git submodule update`. Note that the
submodule *section names* in `.gitmodules` still read `source/JUCE` etc. for the
four that were relocated during the 2026-08 restructure — git keeps the name as
an identifier while rewriting only `path`. Renaming them would require moving
`.git/modules/*` in every existing clone, so they were left alone.

### Our forks — patches live in the fork, rebase and bump

| Directory | Upstream | Our fork | What we changed |
|---|---|---|---|
| `JUCE` | juce-framework/JUCE | dsp56300/JUCE | VST3 program-change → MIDI CC parameter emulation (EMU-59) and `IParameterFinder` support; X11 unconsumed-key forwarding; `getComponentAt` made virtual; removal of the synthetic mouse-move events; Linux kdialog directory multi-select; Android CMake patch; Sonoma build fix |
| `RmlUi` | mikke89/RmlUi | dsp56300/RmlUi | Multi-instance `CoreInstance` adaptation across the Lua and SVG plugins, plus the sandboxed Lua scripting work (EMU-70): restricted standard library, per-instance error logging, script execution timeout. Also visibility-event dispatch fixes |
| `clap-juce-extensions` | free-audio/clap-juce-extensions | dsp56300/clap-juce-extensions | Windows text-field keyboard input fix (BUG-10159) and a `JUCE_VERSION` guard around `WindowsHooks` |
| `cpp-terminal` | jupyter-xeus/cpp-terminal | dsp56300/cpp-terminal | Portability only: Windows ARM and Raspberry Pi arm64 compile fixes, one missing include |

The "what we changed" column is summarised from each fork's commit log — read the
fork itself before assuming it is complete.

### Unpatched — just bump the submodule

| Directory | Upstream |
|---|---|
| `freetype` | freetype/freetype |
| `lunasvg` | sammycage/lunasvg (bundles PlutoVG) |

## Vendored copies

In-tree, no submodule, no upstream link. These have diverged and are effectively
frozen: updating one means re-vendoring by hand and reconciling the differences.
None of them is under active upstream tracking.

| Directory | Project | Notes |
|---|---|---|
| `fst` | FST — Free Studio Technology | Clean-room VST2 plugin headers. Used as the VST2 SDK fallback when the real SDK is not available, see `source/cmake/findvst2.cmake`. **Patched:** `fstSpeakerArrangement_::speakers` is `[8]`, not a flexible array — see the comment on it in `fst/fst.h` |
| `libresample` | libresample (Dominic Mazzoni, after Julius Smith's resample) | Autotools upstream; the `CMakeLists.txt` here is ours |
| `portaudio` | PortAudio | Used by the Vavra test console only |
| `portmidi` | PortMidi (Roger B. Dannenberg) | Old release. A clone of current upstream exists as `portmidi-latest` on some branches |
| `ptypes` | C++ Portable Types Library | Networking primitives behind `framework/networkLib` |
| `lua` | Lua 5.4.7 | Compiled as C++ for the RmlUi Lua bindings |
| `vstsdk2.4.2` | Steinberg VST2 SDK | Not redistributable, so only a `CMakeLists.txt` is committed. The SDK itself is fetched via rclone by `source/cmake/findvst2.cmake`, which falls back to `fst` when unavailable |

Some branches additionally carry `SDL`, `asmjit` and `portmidi-latest` here as
plain upstream clones. Those are committed as gitlinks without matching
`.gitmodules` entries, which is a bug on those branches rather than an intended
tier.
