# Gearmulator

## Emulation of classic VA synths of the late 90s/2000s that used the Motorola 56300 family DSP

This project aims at emulating various musical devices that used the Motorola 56300 family DSPs. At the moment, it can run the Access Virus B, C, Classic and Rack XL

Standalone VST3 and AU plugins are supported. VST2 is supported, too, but only if you provide the VST2 SDK, otherwise, the VST2 build is skipped.

The emulator should compile just fine on any platform that supports C++17, no configure is needed as the code uses C++17 standard data types. For performance reasons, it makes excessive use of C++17 features, for example to parse opcode definitions at compile time and to create jump tables of template permutations, so C++17 is a strong requirement.

The build system used is [cmake](https://cmake.org/).

Contributions are welcome! Feel free to send PRs.

### Join us on Discord

If you want to help or just want to follow the state of the project, feel free to join us on Discord: https://discord.gg/mveFUNbNCK

### Visit our Homepage

🎵 Visit our homepage for Audio and Video examples 🎧:
[DSP 56300 Emulation Blog](https://dsp56300.wordpress.com/)
