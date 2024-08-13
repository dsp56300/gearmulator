# Gearmulator

[![CMake][s0]][l0] [![Nightly][s1]][l1] ![GPLv3][s2] [![Discord][s3]][l3]

[s0]: https://github.com/dsp56300/gearmulator/actions/workflows/cmake.yml/badge.svg
[l0]: https://github.com/dsp56300/gearmulator/actions/workflows/cmake.yml

[s1]: https://github.com/dsp56300/gearmulator/actions/workflows/nightly.yml/badge.svg
[l1]: https://github.com/dsp56300/gearmulator/actions/workflows/nightly.yml

[s2]: https://img.shields.io/badge/license-GPLv3-blue.svg

[s3]: https://img.shields.io/discord/829099347975208970?label=Discord
[l3]: https://discord.gg/WJ9cxySnsM

## Emulation of classic VA synths of the late 90s/2000s that used the Motorola 56300 family DSP

This project emulates various musical devices that used the Motorola 56300 family DSPs.

The supported plugin formats are [FST](https://github.com/pierreguillot/FTS), VST3, AU, [CLAP](https://cleveraudio.org/) and [LV2](https://lv2plug.in/).

Supported architectures: 64 bit x86 aka x64 and ARM aarch64 (Raspberry Pi, Apple Silicon). Note that 32 bit
architectures are not supported!

Platforms: Windows 7+, macOS 10.13+, Linux

At the moment, the following synthesizers are supported:

* Osirus: Access Virus A,B,C
* OsTIrus: Access Virus TI/TI2/Snow
* Vavra: Waldorf microQ
* Xenia: Waldorf Microwave II/XT
* Nodal Red 2x: Clavia Nord Lead/Rack 2x

### Compiling

The emulator compiles on any platform that supports C++17.

The build system used is [cmake](https://cmake.org/).

#### cmake options

| Variable | Description | Default |
|--|--|--|
| gearmulator_BUILD_JUCEPLUGIN | Build Juce based audio plugins | on |
| gearmulator_BUILD_JUCEPLUGIN_CLAP | Build CLAP plugins | on |
| gearmulator_BUILD_JUCEPLUGIN_LV2 | Build LV2 plugins | on |
| gearmulator_BUILD_FX_PLUGIN | Additionally build FX versions of all plugins | off |

Additional options to select which devices to build:

| Variable | Description | Default |
|--|--|--|
| gearmulator_SYNTH_OSIRUS | Build Osirus | on |
| gearmulator_SYNTH_OSTIRUS | Build OsTIrus | on |
| gearmulator_SYNTH_VAVRA | Build Vavra | on |
| gearmulator_SYNTH_XENIA | Build Xenia | on |
| gearmulator_SYNTH_NODALRED2X | Build Nodal Red 2x | on |

### Join us on Discord

If you want to help or just want to follow the state of the project, feel free to join us on Discord: https://discord.gg/WJ9cxySnsM

### Visit our Homepage

🎵 Visit our homepage for Audio and Video examples 🎧:
[DSP 56300 Emulation Blog](https://dsp56300.wordpress.com/)
