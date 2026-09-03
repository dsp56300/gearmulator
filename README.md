# Gearmulator

[![CMake][s0]][l0] [![Nightly][s1]][l1] ![GPLv3][s2] [![Discord][s3]][l3]

[s0]: https://github.com/dsp56300/gearmulator/actions/workflows/cmake.yml/badge.svg
[l0]: https://github.com/dsp56300/gearmulator/actions/workflows/cmake.yml

[s1]: https://github.com/dsp56300/gearmulator/actions/workflows/nightly.yml/badge.svg
[l1]: https://github.com/dsp56300/gearmulator/actions/workflows/nightly.yml

[s2]: https://img.shields.io/badge/license-GPLv3-blue.svg

[s3]: https://img.shields.io/discord/829099347975208970?label=Discord
[l3]: https://discord.gg/WJ9cxySnsM

## Low-Level Emulation of classic VA synths & effects of the late 90s/2000s

*The Usual Suspects proudly presents:*

* **Osirus**: Access Virus A,B,C
* **OsTIrus**: Access Virus TI/TI2/Snow
* **Vavra**: Waldorf microQ
* **Xenia**: Waldorf Microwave II/XT
* **Nodal Red 2x**: Clavia Nord Lead/Rack 2x
* **JE-8086**: Roland JP-8000

**VST2 · VST3 · AU · CLAP · LV2 · STANDALONE**
~ *Also available as FX versions.* ~

**Architectures**: x64, ARM64 (aarch64)

**Platforms**: Windows 7+, macOS 10.13+, Linux

### Compiling

Gearmulator requires a C++17-compatible toolchain.

The build system uses CMake. A Makefile provides a convenient
command-line interface for selecting synthesizers, plugin formats,
and build options.

Run `make help` for the complete list of Make options and targets.

A `CMakePresets.json` is provided for IDE integration. A full list of CMake
options is provided below.

## Quick Start

### Prerequisites

#### 1. Install Dependencies

##### Linux
```sh
make install-deps
```

##### macOS

Install the Xcode Command Line Tools:

```sh
xcode-select --install
```

Install the build tools

```sh
brew install cmake ninja
```

##### Windows

Run the following in PowerShell as administrator:

```powershell
iwr -useb https://raw.githubusercontent.com/gearmulator/scripts/install_windows_dependencies.ps1 | iex
```

This will install (or update) the MSVC toolchain, Git, MSYS2, CMake, Make, and Ninja, and add the required tools to your system `PATH`.

#### 2. Update Submodules

```sh
git submodule update --init --recursive
```

#### 3. Build

See the available products, formats, build options, and install targets:

```sh
make help
```

For example,

```sh
make OSTIRUS=1 VST3=1 FX=1
```

#### 4. Install

```sh
make install
```

### CMake Options

| | | |
|--|--|--|
| **Available synthesizers:** | | | |
| gearmulator_SYNTH_OSIRUS | Osirus | on |
| gearmulator_SYNTH_OSTIRUS | OsTIrus | on |
| gearmulator_SYNTH_VAVRA | Vavra | on |
| gearmulator_SYNTH_XENIA | Xenia | on |
| gearmulator_SYNTH_NODALRED2X | Nodal Red 2x | on |
| gearmulator_SYNTH_JE8086 | JE-8086 | on |
| | | |
| **Output formats:** | | |
| gearmulator_BUILD_JUCEPLUGIN_VST2 | VST2 | on |
| gearmulator_BUILD_JUCEPLUGIN_VST3 | VST3 | on |
| gearmulator_BUILD_JUCEPLUGIN_CLAP | CLAP | on |
| gearmulator_BUILD_JUCEPLUGIN_LV2 | LV2 | off |
| gearmulator_BUILD_JUCEPLUGIN_AU | Audio Unit | on |
| gearmulator_BUILD_JUCEPLUGIN_Standalone | Standalone application | off |
| gearmulator_BUILD_FX_PLUGIN | FX version | off |
| | | |
| **Build configuration:** | | |
| gearmulator_BUILD_JUCEPLUGIN | Enable JUCE-based targets | on |
| gearmulator_ENABLE_LTO | Enable link-time optimization | on |
| gearmulator_ENABLE_THIRDPARTY_WARNINGS | Display warnings emitted by bundled dependencies | off |

## What is Gearmulator?

Gearmulator uses low-level emulation of the ICs found in supported
synthesizers and effects processors to run their original firmware (ROM).

### Join us on Discord

If you want to help or just want to follow the state of the project, feel free to join us on Discord: https://discord.gg/WJ9cxySnsM

### Visit our Homepage

🎵 Visit our homepage for Audio and Video examples 🎧:
[The Usual Suspects Website](https://dsp56300.com/)
