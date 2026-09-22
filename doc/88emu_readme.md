# 88emuPlayer

**88emu** emulates the hardware inside Roland Sound Canvas modules, related PCM sound generators, and the MT-32 and CM-32L family of LA synthesizers. It runs the original firmware on emulated CPUs and sound chips, including the firmware's instrument selection, voice allocation, effects, MIDI handling and front-panel behavior. You supply the ROM images; they are not included.

**88emuPlayer** is the standalone application for playing MIDI files and using those devices from a MIDI keyboard, sequencer or game. **88EmuCli** uses the same emulation to render files to WAV offline, as fast as the computer can run it.

## Getting started and file locations

1. Put a complete ROM set in the player's data folder, or a subfolder such as `roms/SC-88Pro`.
2. Start 88emuPlayer and choose a device from the device selector. Missing-ROM entries show the required files, sizes and known MD5 hashes.
3. In **Settings → Audio**, select an output and adjust the buffer size if necessary.
4. Add a supported song to the playlist, or give a MIDI input a part group in **Settings → MIDI**. Let the device finish booting before sending notes; **Fast Boot** can do this before the panel opens.

**macOS may block the first launch.** macOS quarantines programs downloaded from the internet and can refuse to open 88emuPlayer or 88EmuCli the first time, saying it could not verify them. Open **System Settings → Privacy & Security**, scroll down to **Security**, click **Open Anyway** next to the message about the program, and confirm. This is needed once per program; for 88EmuCli, run it once in Terminal first so that the message appears. On macOS 14 and earlier, Control-clicking the app in Finder and choosing **Open** also works.

**macOS may keep the player out of your folders.** Documents, Desktop, Downloads and removable or network volumes need your permission, and the data folder is in Documents. When the player cannot read it, the device selector says the ROM folder is not readable instead of listing missing ROMs, and playlist entries it cannot read are marked. Allow 88emuPlayer in **System Settings → Privacy & Security → Files and Folders**, or add it to **Full Disk Access**, then restart it. Started from a terminal, the player has the terminal's permissions instead of its own.

| OS | Default data folder |
| --- | --- |
| Windows | Your Windows Documents folder, then `The Usual Suspects\88emuPlayer` (usually `%USERPROFILE%\Documents\The Usual Suspects\88emuPlayer`) |
| macOS | `~/Documents/The Usual Suspects/88emuPlayer` |
| Linux | `$XDG_DATA_HOME/The Usual Suspects/88emuPlayer`, or `~/.local/share/The Usual Suspects/88emuPlayer` when `XDG_DATA_HOME` is unset |

Inside that folder:

| Location | Contents |
| --- | --- |
| The folder itself and its subfolders | ROM search area; individual uncompressed ROM files can be organized by device. |
| `config/88emuPlayer.xml` | Player settings, including the selected model, audio/MIDI devices, gain, reset policy and skin. |
| `skins/<skin name>/` | External skin files. |

`--rom-dir PATH` replaces the ROM search folder for that launch; it does not move the config or skins. `--config PATH` selects a different settings file. The environment variable `TUS_DATA_FOLDER` replaces the base data location: the player appends `The Usual Suspects/88emuPlayer` to it.

ROMs are rescanned on device selection and **Restart Device**. After adding missing files, select the device again. The config stores application preferences; it is not a saved image of the hardware's battery RAM.

## Emulated hardware and devices

The main CPUs execute the original device firmware as accurately as possible. 88Emu implements low-level high-performance emulators for the following CPUs: Hitachi **H8/500** (H8/532 and H8/510), Hitachi **SH-1** / **SH-2** and Intel **MCS-96**. We do not use JIT for those, but we achieve decent performance thanks to an event-loop driven peripherals implementation and an efficient instruction cache that pre-bakes the opcode handlers as function pointers.

To actually generate the sound in the emulator we implement low-level emulation of different custom sound chips:

- LP (MB87419/MB87420): 32 voices DPCM sample player with 4 point interpolation and volume ramps. Originally started by ValleyBell in MAME,
  developed by TUS from high-level silicon analysis.
- RCC (TC23SC140AF-007): fixed-function 24 bit DSP implementing 32 voices summing/mixing, chorus and reverb. Developed by TUS from low-level silicon analysis.
- GP (TC24SC201AF-002) and GP4 (TC6116AF): 28 voices DPCM sample player with 4 point interpolation, ramps, filter and fixed-function reverb/chorus DSP,
  originally developed by [NukeYKT](github.com/nukeykt/Nuked-SC55) from silicon analysis.
- XP (MBCS30109/MB87B105), XP3 (TC170C200AF-005) and XP6 (TC203C180AF-002): 64 voices DPCM sample player with 4 point interpolation, 5 programmable ramps,
  multi mode filter with ring mod/booster/structures, mixer with 4 busses, fully programmable DSP with 256 steps per sample.
  Developed by TUS from high-level silicon analysis and single-sample-step black box probing. Uses JIT (x86_64 and aarch64) for faster DSP processing.
- LSP (MB87837): 24 bit programmable DSP with 384 steps per sample. Developed by TUS from black box probing.
  Uses JIT (x86_64 and aarch64) for faster DSP processing.
- LA32 (R15229896 / MB87136A): the 32-partial "Linear Arithmetic" synthesizer of the MT-32 and CM-32L, with its square/sawtooth
  generators, resonant filter, PCM sample reader, envelope ramps and ring modulation. Derived from [NukeYKT](https://github.com/nukeykt/Nuked-MT32)'s
  core, improved and hardware-verified by TUS.
- Boss reverb gate array (HG61H20R36F): the MT-32 and CM-32L's fixed-sequence reverb, running the original microcode ROM.
  Derived from Sergey V. Mikayev's emulator; uses JIT (x86_64 and aarch64).

Some devices, like the SC-55mkII, the SC-88 and the SC-8850, use a high level emulation for their sub-mcu, which handles USB and MIDI communication.
Because of this, some MIDI/USB behavior might differ from the original unit, but we are doing our best to fix any inconsistencies we find.
If you find any, please report it as a bug.

| Device | CLI ID | Main CPU | Sound chips | Notes |
| --- | --- | --- | --- | --- |
| MT-32, old board (1.x firmware) | `mt32old` | C8095-90 | LA32 + Boss reverb | experimental |
| MT-32, new board (2.x firmware) | `mt32new` | P8098 | LA32 + Boss reverb | experimental, also runs the MT-100's firmware |
| CM-32L | `cm32l` | P8098 | LA32 + Boss reverb | experimental; responds to MIDI channels 2–10, the LAPC-I firmware runs here too |
| CM-32LN | `cm32ln` | 80C198 | LA32 + Boss reverb | experimental |
| CM-32P | `cm32p` | P8098 | LP + RCC | experimental support; responds to MIDI channels 11–16 |
| CM-64 | `cm64` | P8098 ×2 | LA32 + Boss reverb + LP + RCC | experimental; a CM-32L and a CM-32P in one case, channels 2–10 and 11–16 |
| SC-55 | `sc55` | H8/532 | GP  | |
| SC-55mkII | `sc55mk2` | H8/532 | GP4 | uses high-level sub-mcu emulation |
| SC-55st | `sc55st` | H8/532 | GP4 | uses high-level sub-mcu emulation, no hardware panel present |
| CM-300 / SCC-1 | `cm300` | H8/532 | GP  | no hardware panel present |
| SC-155 | `sc155` | H8/532 | GP  | physical sliders are not supported |
| SC-155mkII | `sc155mk2` | H8/532 | GP4 | physical sliders are not supported |
| SCC-1A | `scc1a` | H8/532 | GP  | no hardware panel present |
| SCB-55 | `scb55` | H8/532 | GP4 | no hardware panel present |
| RLP-3237 | `rlp3237` | H8/532 | GP4 | no hardware panel present |
| SC-88 | `sc88` | H8/510 | XP | uses high-level sub-mcu emulation, dual midi input |
| SC-88VL | `sc88vl` | H8/510 | XP | uses high-level sub-mcu emulation, dual midi input |
| XPGS / G-800 | `xpgs` | H8/510 | XP | dual midi input, emulates the sound generator only, not the arranger |
| SC-88Pro | `sc88pro` | H8/510 | XP3 + LSP | uses high-level sub-mcu emulation, dual midi input |
| VE-GS Pro | `vegspro` | H8/510 | XP3 + LSP | uses high-level sub-mcu emulation, dual midi input, no hardware panel present |
| SC-8820 | `sc8820` | SH7017 (SH-2) | XP6 + LSP | experimental, uses high-level sub-mcu emulation in USB mode, dual midi input, no hardware panel emulated |
| SC-8850 | `sc8850` | SH7016 (SH-2) | 2 × XP6 + LSP | uses high-level sub-mcu emulation in USB mode, 4x midi input |
| NU-10B | `nu10b` | SH7034 (SH-1) | XP | experimental, runs in GM mode only, no hardware panel or display emulated |
| MIIG5 | `miig5` | SH7042A (SH-2) | 2 × XP6 | experimental, runs in GM mode only, no hardware panel or display emulated |


## ROM loading

Known ROMs are identified by **MD5 and size**, so their filenames can be arbitrary. For a custom or unrecognized image, use a listed filename with the **exact** required size.

**Selection priority in this version is filename and size first, then known hashes.** A named custom image can therefore override a recognized stock dump elsewhere in the search folder. A hash mismatch is reported but does not prevent loading; it means the image has not been verified against a registered dump. Avoid leaving conflicting copies in the search area when comparing firmware versions.

The tables below list accepted filenames. Sizes are binary: 1 KiB = 1,024 bytes; 1 MiB = 1,048,576 bytes. All components in a row are required unless an alternative is stated. The device selector's ROM requirements dialog is the complete list of accepted names, revisions and hashes.

| Device | CPU / program / data ROMs | Wave ROMs |
| --- | --- | --- |
| MT-32, old board | `mt32_control.bin` — 64 KiB (firmware 1.04–1.07, Blue Ridge, M-9), **or** the two 32 KiB EPROMs `r15449122.bin` (IC27) and `r15449123.bin` (IC26) under any name that the known hashes recognize; `mt32old_reverb.bin` — 32 KiB (R15179857, the old board's reverb microcode) | `mt32_wave.bin` — 512 KiB (R15449121), **or** `r15179844.bin` and `r15179845.bin` — 256 KiB each |
| MT-32, new board | `mt32_control.bin` — 128 KiB (firmware 2.03–2.07); `mt32new_reverb.bin` — 32 KiB (R15179917, shared with the CM-32L) | as above |
| CM-32L | `cm32l_control.bin` — 64 KiB (CM-32L 1.02 or LAPC-I 1.00); `cm32l_reverb.bin` — 32 KiB (R15179917) | `cm32l_wave.bin` — 1 MiB, **or** `r15449121.bin` and `r15179945.bin` — 512 KiB each |
| CM-32LN | `cm32ln_control.bin` — 64 KiB (CM-32LN / CM-500 / LAPC-N 1.00); `cm32l_reverb.bin` — 32 KiB | the CM-32L's |
| CM-32P | `cm32p_program.bin` — 64 KiB | `cm32p_wave0.bin` — 512 KiB; `cm32p_wave1.bin` — 512 KiB; `cm32p_wave2.bin` — 512 KiB |
| CM-64 | the CM-32L's and the CM-32P's sets together | |
| SC-55 | `sc55mk1_internal.bin` — 32 KiB; `sc55mk1_program.bin` — 256 KiB | `sc55mk1_wave0.bin` — 1 MiB; `sc55mk1_wave1.bin` — 1 MiB; `sc55mk1_wave2.bin` — 1 MiB |
| SC-55mkII | `sc55mk2_internal.bin` — 32 KiB; `sc55mk2_program.bin` — 512 KiB | `sc55mk2_wave0.bin` — 2 MiB; `sc55mk2_wave1.bin` — 1 MiB |
| SC-55st | `sc55st_internal.bin` — 32 KiB; `sc55st_program.bin` — 512 KiB | `sc55st_wave0.bin` — 2 MiB; `sc55st_wave1.bin` — 1 MiB |
| CM-300 / SCC-1 | `cm300_internal.bin` — 32 KiB; `cm300_program.bin` — 256 KiB | `cm300_wave0.bin` — 1 MiB; `cm300_wave1.bin` — 1 MiB; `cm300_wave2.bin` — 1 MiB |
| SC-155 | `sc155_internal.bin` — 32 KiB; `sc155_program.bin` — 256 KiB | `sc155_wave0.bin` — 1 MiB; `sc155_wave1.bin` — 1 MiB; `sc155_wave2.bin` — 1 MiB |
| SC-155mkII | `sc155mk2_internal.bin` — 32 KiB; `sc155mk2_program.bin` — 512 KiB | `sc155mk2_wave0.bin` — 2 MiB; `sc155mk2_wave1.bin` — 1 MiB |
| SCC-1A | `scc1a_internal.bin` — 32 KiB; `scc1a_program.bin` — 256 KiB | `scc1a_wave0.bin` — 1 MiB; `scc1a_wave1.bin` — 1 MiB; `scc1a_wave2.bin` — 1 MiB |
| SCB-55 | `scb55_internal.bin` — 32 KiB; `scb55_program.bin` — 256 KiB | `scb55_wave0.bin` — 2 MiB; `scb55_wave1.bin` — 1 MiB |
| RLP-3237 | `rlp3237_internal.bin` — 32 KiB; `rlp3237_program.bin` — 256 KiB | `rlp3237_wave0.bin` — 2 MiB |
| SC-88 | `sc88_control.bin` — 512 KiB | `sc88_wave0.bin` — 2 MiB; `sc88_wave1.bin` — 2 MiB; `sc88_wave2.bin` — 2 MiB; `sc88_wave3.bin` — 2 MiB |
| SC-88VL | `sc88vl_control.bin` — 512 KiB | `sc88vl_wave0.bin` — 2 MiB; `sc88vl_wave1.bin` — 2 MiB; `sc88vl_wave2.bin` — 2 MiB; `sc88vl_wave3.bin` — 2 MiB |
| XPGS / G-800 | `xpgs_control.bin` — 512 KiB | `xpgs_wave0.bin` — 2 MiB; `xpgs_wave1.bin` — 2 MiB; `xpgs_wave2.bin` — 2 MiB; `xpgs_wave3.bin` — 2 MiB; `xpgs_wave4.bin` — 2 MiB |
| SC-88Pro | `sc88pro_control.bin` — 1 MiB | `sc88pro_wave0.bin` — 8 MiB, **or** `sc88pro_wave_cs0.bin` and `sc88pro_wave_cs1.bin` — 4 MiB each; `sc88pro_wave1.bin` — 8 MiB, **or** `sc88pro_wave_cs2.bin` and `sc88pro_wave_cs3.bin` — 4 MiB each; `sc88pro_wave2.bin` — 4 MiB |
| VE-GS Pro | `vegspro_control.bin` — 1 MiB | `vegspro_wave0.bin` — 8 MiB; `vegspro_wave1.bin` — 8 MiB; `vegspro_wave2.bin` — 4 MiB |
| SC-8820 | `sc8820_internal.bin` — 128 KiB, or the supported reconstructed 64 KiB image; `sc8820_program.bin` — 2 MiB | `sc8820_wave0.bin` — 16 MiB; `sc8820_wave1.bin` — 8 MiB |
| SC-8850 | `sc8850_internal.bin` — 64 KiB; `sc8850_program.bin` — 1 MiB; `sc8850_data.bin` — 2 MiB | `sc8850_wave.bin` — 32 MiB; **or** `sc8850_wave0.bin` and `sc8850_wave1.bin` — 16 MiB each |
| NU-10B | `nu10b_internal.bin` — 64 KiB; `nu10b_program.bin` — 1 MiB | `nu10b_wave0.bin` — 2 MiB; `nu10b_wave1.bin` — 2 MiB; `nu10b_wave2.bin` — 2 MiB; `nu10b_wave3.bin` — 2 MiB |
| MIIG5 | `miig5_internal.bin` — 256 KiB; `miig5_program.bin` — 2 MiB | `miig5_wave.bin` — 32 MiB, already unscrambled |

SC-88Pro and VE-GS Pro may also use the compatible donor waves described below. The standardized names select a device and layout; they do not change the required byte order or contents.

Additional supported layouts:

- The old MT-32 board's firmware sits in two EPROMs on a 16-bit bus, IC27 holding the even bytes and IC26 the odd ones. The 64 KiB `mt32_control.bin` is the two interleaved, which is how munt's `MT32_CONTROL.ROM` and the `mt32_control.rom` of the usual dump sets already come; a pair of 32 KiB chip dumps is joined the same way. The new board's 128 KiB firmware exists only as one image; the two boards do not run each other's firmware, and each needs its own reverb microcode.
- The MT-32 PCM set is the single 512 KiB mask ROM R15449121 (munt's `MT32_PCM.ROM`) or the two 256 KiB chips of the earliest boards; the CM-32L's 1 MiB image (munt's `CM32L_PCM.ROM`) is that same ROM followed by R15179945, and both halves are accepted separately. Files named by the registry's part numbers, `r15449121.bin` and so on, serve every board that carries the chip.
- The SC-55's on-chip ROM and its program ROM belong to one firmware revision (1.00, 1.10, 1.20/1.21, 2.00) and are loaded as a pair: with several revisions in the folder the newest complete pair is used, and a program ROM whose on-chip ROM is missing does not make the board available.
- The six program ROMs produced by [sc55mk2-ctf-patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher) are recognized as SC-55mkII firmware: they restore the SC-55's capital tone fallback for variations the mkII leaves silent, with the SC-55's, strictly the SC-55's or the mkII's own tone list, and the SC-55 v1.21 or v2.00 drum set ranges.
- Recognized H8 control ROMs can be loaded in CPU byte order or the supported word-swapped dump order. Recognized 1 MiB Pro control images embedded in repeated 2/4 MiB dumps are also accepted.
- The SC-88Pro board carries its PCM on five 4 MiB mask ROMs, one per chip select, instead of the three larger VE-GS Pro parts that `sc88pro_wave0.bin` to `sc88pro_wave2.bin` hold. Dumps of the five are also accepted: CS0 and CS1 together replace `sc88pro_wave0.bin`, CS2 and CS3 replace `sc88pro_wave1.bin`, and CS4 is identical to `sc88pro_wave2.bin`. The hashes registered for CS0–CS3 were derived by splitting the VE-GS Pro images, not taken from dumps of SC-88Pro chips.
- SC-88Pro and VE-GS Pro can obtain their waves from recognized decoded SC-8850 or SC-8820 wave images. Their own control ROM is still required.
- Recognized `SCCore.dll` / `SCCore00.dylib` containers can supply the SC-8820 wave regions and compatible Pro waves. They are scanned as data, never executed. They do not supply CPU firmware. For SC-8850 they supply **bank A only**; you still need its distinct bank B.
- SC-8850 also accepts recognized raw XP wave layouts.
- The patched [sc55mk2-ctf-patcher](https://github.com/shingo45endo/sc55mk2-ctf-patcher) SC-55mkII GS-28 2.00 ROM patched is supported.
- SC-88Pro also accepts a second recognized control ROM from an SC-GS board, which identifies itself as "SC-GS A '96" rather than by a version number. If both it and the 1.02 firmware are found, 1.02 is used unless the SC-GS image is the one named `sc88pro_control.bin`.

## Playlist and MIDI playback

Use **Add** to choose multiple files, or drop files onto the playlist. Click an entry to start it, drag rows to reorder them, and use **X** to remove an entry. Right-click the playlist for **Clear Playlist**. Play/Pause controls the current song; Stop silences it and returns to its start.

Songs advance in playlist order. After the last MIDI event, the player allows a four-second release/effects tail, then the configured pause before the next song. The list stops at its end. Transport changes silence old notes, and each new song applies the selected reset policy.

A playlist entry whose file cannot be read keeps its place, shown in red, and playback skips it. That happens when the file was moved or deleted, is on a drive that is not connected, or is in a folder macOS does not let the player read. Click the entry to try again once the file is back; if it still cannot be read, the player says why. Loading a playlist and restoring the last one both keep such entries and list them in a message. A file you add on its own that cannot be read is refused instead.

| Extensions | Format |
| --- | --- |
| `.mid`, `.midi` | Standard MIDI Files, including tempo changes and embedded SysEx |
| `.rcp`, `.r36` | Recomposer RCM-PC98 V2 |
| `.g36` | Recomposer RCP3 / V3 |

**Live MIDI:** in Settings → MIDI, click **A**, **B**, **C** or **D** next to a MIDI input to choose the part groups it plays. An input with no group lit is closed, and one with several lit plays all of them. A virtual input lets another application send directly to 88emuPlayer; select, for example, **88emu MIDI IN B** as that application's destination to play group B. Each group has its own 16 channels. Groups the selected device doesn't have are dimmed, and messages for them are ignored. The MIDI Output setting selects where messages emitted by the emulated device go.

**Recording:** **Record to WAV** captures the player's stereo output in real time, including live MIDI, gain and the optional limiter. **Stop and Save** asks where to save the 24-bit WAV. For unattended file conversion without real-time waiting, use 88EmuCli.

## Resets and device quirks

### Reset before each song

The reset setting prepares the selected device before a playlist song or CLI render. It does not replace the firmware or change which device is emulated.

| Mode | What it does | When to use it |
| --- | --- | --- |
| **Off** | Skips the reset SysEx. The player still silences old notes and resets channel controllers at a new song. | Externally prepared setups, or files that supply their own initialization. |
| **GM** | Sends GM System On. The firmware decides how it initializes its GM-compatible mode. | General MIDI arrangements. Early firmware support varies; an original SC-55 revision may not respond like a later GM device. |
| **GM2** | Sends GM2 System On (`F0 7E 7F 09 03 F7`). The firmware decides how it initializes its GM2 mode. | General MIDI 2 arrangements on a GM2 device such as the SC-8850 or SC-8820. Models older than GM2 do not implement the message. |
| **GS** (default) | Sends Roland GS Reset to initialize the GS sound map and part/effect settings. | Sound Canvas / GS arrangements and a predictable GS starting state. |
| **MT-32** | Sends GS Reset, then sets up the SC-55-style MT-32 instrument arrangement for channels 1–10, including banks, programs, pan, level and effects sends. Channels 11–16 retain GS defaults. | Older music using the MT-32 preset arrangement on a GS device. |

**MT-32 here is a GS compatibility arrangement for the Sound Canvas family, not LA synthesis.** On a GS device it cannot reproduce custom MT-32 timbres or interpret MT-32 patch-upload SysEx as an MT-32 would; for that, select one of the MT-32 or CM-32L devices, which run the real firmware.

**The MT-32, CM-32L, CM-32LN, CM-32P and CM-64 predate GM.** Their firmware ignores GM, GM2 and GS System On, and knows one reset of its own, the "all parameters reset" data set to address `7F 00 00` (`F0 41 10 16 12 7F 00 00 00 01 F7`). On those devices every mode but **Off** sends that message instead, and the MT-32 arrangement step is skipped since the device is the real thing. The reset takes the module back to its power-on state: factory timbres, patches and system settings; the SysEx of the song that follows loads what it needs.

GM and GS resets get 200 ms to settle, and GM2 400 ms, because GM2 System On keeps the SC-8850 busy for about 310 ms. The MT-32 arrangement uses two 200 ms phases. Messages inside the song are sent afterward and can change the mode again. The song-reset option applies when starting songs; it is not an automatic reset for every live MIDI connection.

To reset the device at any other time, use **Send GM Reset**, **Send GM2 Reset** or **Send GS Reset** in the context menu. Each sends the same message as the song reset, to every part group. On the MT-32 and CM devices the menu offers **Send All Parameters Reset** instead. **Send All Notes Off** lifts the hold pedal and sends All Notes Off on every channel, which is what stops a note on every board: the LA and CM firmware has no All Sound Off.

### MT-32 and CM-32L: the LA boards

The MT-32 (1987) and the CM-32L (1989) are Roland's LA synthesizers, "Linear Arithmetic": 32 partials of square/sawtooth waves through a fake resonant filter (phase distortion), or PCM samples, mixed per timbre, with a stereo digital reverb. The CM-32L is the MT-32's board with 33 extra PCM sounds ("sound effects" for games), in a case without the display, the switches and the knob; the LAPC-I is a CM-32L on an ISA card and runs the same firmware. 88emu runs the original 8095/8098 firmware on the emulated LA32, reverb gate array and display controller.

**Two MT-32 boards.** Roland built the MT-32 on two different main boards, which the service notes call the old and the new type, and they are two devices here because they take different ROMs and sound different:

- **MT-32 old board (`mt32old`)** runs the 1.x firmware (1.04 to 1.07, and the third-party Blue Ridge and M-9 firmwares) from two EPROMs, with the reverb microcode R15179857. Its 8095 sits on a 16-bit bus, the LA32 is addressed word-wide and the DAC is wired one bit up from the audio bus with bit 14 dropped: the board has no volume VCA, master volume is scaled digitally, and its reverb return is mixed at unity.
- **MT-32 new board (`mt32new`)** runs the 2.x firmware (2.03 to 2.07) from a single banked 128 KiB ROM, whose upper half carries the ROM Play demo songs, with the reverb microcode R15179917. It is the CM-32L's design in the MT-32's case: the same 8098, the same PWM-driven VCA, and the LA32's output arrives on the audio bus rotated up one bit - a sound is twice what the chip computed and folds back past three quarters of full scale, the overdrive the MT-32 is known for. The MT-100 is this board with a sequencer, and its firmware 2.03 runs here.

The two firmware generations are not interchangeable: 1.x only boots on the old board and 2.x only on the new one, and each board needs its own reverb microcode. The 2.x firmware and the CM-32L's differ from 1.x in details munt documents as its "quirks" - pitch and envelope overflow behaviour, the ring modulator, the pan law, the key shift, the display; here they are simply what the firmware does on the emulated board.

**MIDI.** From power-on, the eight parts answer on **channels 2–9** and the rhythm part on **channel 10**; channel 1 is silent until a SysEx or the panel reassigns a part. Send to **group A, channel 2** for part 1. The pan convention is reversed relative to GM (CC10 = 0 is right, 127 is left), All Sound Off (CC120) is not recognized - the player uses hold pedal up plus All Notes Off instead when it stops or seeks - and the only reset is the all parameters reset described above. The boards' own MIDI OUT carries their SysEx replies (data requests, handshaking); the CM-32P half of a CM-64 transmits nothing.

**Front panel.** The MT-32's 20-character display and MIDI MESSAGE lamp are shown in the CM-32L bezel for now, and its ten switches and the VOLUME/VALUE knob are on the keyboard; see the shortcuts below. The knob is a potentiometer read by the CPU's A/D converter, so it stays where it was turned. The CM-32L's own firmware drives the same display and scans the same switches although its case has neither: the player shows the display as a service screen, and the MT-32 keys reach its switch matrix too. That is how the firmware's hidden screens are entered on every LA board: hold **3 + V** (PART 3 + VOLUME) while switching the power on for the test mode, or **4 + R + M** (PART 4 + RHYTHM + MASTER VOLUME) for the firmware version.

**Level.** The LA boards' line output is louder than the digital Sound Canvas boards' for the same DAC word, as their output stages are modelled from the schematic (see the analog section), and the new board's bus rotation doubles the LA32's word on top of that. Loud passages can exceed full scale; use the player's volume control or the limiter.

**CM-32LN.** The CM-32LN, the CM-500's LA half and the LAPC-N run a later control ROM on an 80C198, which takes two clocks per state where the 8095 and 8098 take three; the vibrato of those units is known to run faster for it. The device is catalogued (from munt's published digest) and emulated with the faster CPU, but no dump has been available to us to test it.

### CM-32P, CM-64 and expansion cards

The CM-32P is a PCM sound module related to the PCM half of the CM-64 and Roland's U-series sample instruments. It complements the LA sound generator found in an MT-32/CM-32L; it is neither that LA synthesizer nor a General MIDI Sound Canvas. Its instrument map is different. The CM-64 is a CM-32L and a CM-32P in one case: both boards run their own firmware, share the MIDI IN and the volume knob, and their outputs meet at the PCM board's mixer, so it answers on **channels 2–10 (LA) and 11–16 (PCM)** at once and shows both boards' service displays.

- At startup the CM-32P's six parts receive on **MIDI channels 11–16**. Send to **group A, channel 11** for the first part; a keyboard sending on channel 1 will normally produce no sound.
- The GM, GM2 and GS resets mean nothing to it; every song-reset mode but Off sends the all parameters reset, and a GM or GS song does not get remapped into General MIDI.
- Its pan convention is reversed relative to GM: CC10 = 0 favors right, and 127 favors left. Like the LA boards it has no All Sound Off.
- The player exposes the board's service LCD, although the original module has no normal front-panel display. Display behavior is firmware-dependent; service-mode LCD timing remains under investigation.
- The emulation is experimental. Booting and playing notes do not establish complete physical audio/effects fidelity.

**Expansion cards.** The CM-32P and the CM-64 have a slot for the SN-U110 series PCM cards. Choose a card image (up to 512 KiB, in either of the two dump byte orders, which the loader tells apart from the card's tone list) in **Settings → General → CM-32P / CM-64 PCM card**; the choice is saved and the card is in the slot whenever one of the two devices loads, including after a device switch, Power on or Restart. `--pcm-card PATH` selects a card for one launch in both programs, without changing the saved one.

```sh
88emuPlayer --device cm32p --reset off --pcm-card "/path/to/SN-U110-card.bin"

88EmuCli --device cm64 --rom-dir "/path/to/roms" --reset off \
  --pcm-card "/path/to/SN-U110-card.bin" \
  --output "cm64-card.wav" "cm64-song.mid"
```

The song must select card tones using MIDI Program Change values starting at **64 / 0x40** (often displayed as program **65** in software that numbers programs from 1). Those tones are silent when no card is inserted.

### SC-55 family, expansion boards and SC-8820

The SC-55 and SC-55mkII have different firmware, voice-generation timing and behavior for unsupported instrument banks. Selecting an SC-55 map on a later device changes its sound map; it does not turn the later hardware into an original SC-55. Use the actual SC-55 model when those differences matter. The optional pre-patched mkII ROM described above supplies capital-tone fallback for otherwise missing variations.

SCB-55, SCC-1A and the other card profiles do not have the module's front-panel controls. XPGS emulates the G-800 GS sound engine, not its arranger. VE-GS Pro uses its own panel-less firmware. “No display” on these devices is expected. SC-88VL lacks the SC-88/Pro Preview button.

**SC-8820 is experimental.** It currently supports a reconstructed **BAD_DUMP** of the internal CPU ROM. It has **not been tested with a correct internal ROM dump**. Matching the registered hash identifies that reconstructed image; it is not evidence of an authentic dump or complete hardware compatibility. Its external program and waves are separate requirements.

The SC-88Pro firmware also has an XG compatibility mode. Send XG System On (`F0 43 10 4C 00 00 7E 00 F7`) from the song or your MIDI source. If you enable it externally, choose Reset Off so a song-start GS Reset does not undo it. This is the Roland firmware's compatibility mode; full Yamaha XG voice/effects equivalence is not established.

### Factory initialization, Fast Boot and Restart

Real hardware expects initialized battery-backed SRAM. A newly created emulated board starts with blank memory, like a unit with a depleted backup battery. Firmware can then start with odd part assignments, levels or incomplete user/system settings. For example, the SC-155 can start with part 1 as drums on channel 10, and the SC-155mkII on part 8 at level 84. A per-song GM/GS reset does not initialize every battery-backed setting.

**Factory Reset on load**, enabled by default, runs the firmware's full factory-initialization procedure on SC-55, SC-55mkII, SC-155, SC-155mkII, SC-88, SC-88VL, SC-88Pro and SC-8850, then power-cycles the initialized board. Leave this enabled for ordinary playback. The other profiles currently have no equivalent automated procedure; the MT-32 and CM boards initialize their own memory at power-on and need none.

**Fast Boot (skip intro)**, off by default, runs another ten seconds of emulated time before exposing the device for use. It skips waiting through the firmware's intro in real time, but takes extra computation during loading. It is independent of factory initialization. Both settings apply at the next device selection, power-on or Restart.

**Restart Device** stops playback, rescans ROMs and constructs a fresh device. It clears SRAM and all runtime hardware state, including edits, controller state, voices and effects buffers, then applies the startup settings above. It retains the playlist and application settings and does not rewrite the ROM files. Use it for a clean start or after changing ROMs.

**Q** is the front-panel POWER switch, and it behaves as the selected model's switch does. On SC-55, SC-55mkII, SC-155, SC-155mkII and SC-88VL, POWER is a key the firmware reads, not a supply switch. The unit stays powered, and a press puts it into its standby mode. In standby the display and lamps are dark, the sound is silenced, and incoming MIDI is ignored. The next press wakes it. The emulation keeps running through standby, as the hardware does, and playback is not stopped.

SC-88, SC-88Pro, SC-8820 and SC-8850 use power on/off, as do SC-55st, the MT-32s and the CM boards; the SC-55 standby behavior does not apply to every model carrying the SC-55 name.

On models without standby, and with **Shift+Q** on the five standby models, the switch cuts the power supply. Power-off stops playback and discards incoming MIDI; an active WAV recording continues with silence. Power-on starts a fresh board using the startup settings, except that held panel buttons bypass automatic Factory Reset and Fast Boot for that boot. Volatile device state is not preserved across power-off.

## Settings

Open **Settings** from the application's context menu. The panel has six pages.

| Page / setting | Purpose |
| --- | --- |
| **General — Reset before each song** | Off, GM, GM2, GS or MT-32 arrangement; see the reset table. Default GS. On the MT-32 and CM devices every mode but Off sends their own all parameters reset. |
| **General — Pause between songs (ms)** | Extra silence between automatically advanced songs, after the four-second tail. 0–60,000 ms; default 1,000. |
| **General — Warn on unknown ROM hash on load** | Show warnings when a named ROM does not match the known hashes. |
| **General — Factory Reset on load** | Initialize battery-backed settings through the firmware where supported. Default on. |
| **General — Fast Boot (skip intro)** | Advance the board through its startup screens during loading. Default off. |
| **General — CM-32P / CM-64 PCM card** | The SN-U110 series card image in the slot of those two devices, or none. Saved; a changed card takes effect at the next device load, which the setting offers. |
| **Skins** | Activate a skin, export the embedded skin, or open the skins folder. |
| **Interface — Force software rendering** | Draw the interface on the CPU instead of the graphics card (Metal on macOS, OpenGL on Windows and Linux). Useful for incompatible graphics hardware or drivers; can increase CPU use. This changes graphics rendering, not audio emulation. |
| **Interface — Window scale** | Scale the interface from 50% to 300%. |
| **Audio — Driver / Output** | Choose the audio backend and output device. **Use system device** follows the default output on CoreAudio and Windows Audio; selecting a named device fixes the choice. ASIO requires an explicit driver selection. |
| **Audio — Left / Right output channel** | Route stereo to a chosen pair of hardware outputs. Choosing the other side's channel swaps the pair. Routing is remembered per device; WAV recordings retain logical left/right order. |
| **Audio — Sample rate** | Set the host output rate. The emulated board retains its native clock; a resampler converts between them. |
| **Audio — Buffer size** | Smaller buffers reduce live-playing latency; larger buffers give the computer more time and can avoid dropouts. |
| **Audio — Driver Control Panel** | Open the selected driver's own settings when available. |
| **Audio — Apply limiter to output** | Enable stereo-linked peak limiting after the player's gain. Default off; details below. |
| **Audio — Resampler** | Choose Legacy, MAME High Quality or MAME Lo-Fi. Default MAME High Quality. |
| **Audio — Analog Output Emulation** | Off, Auto, or a named output circuit; see the analog table. Default Off. |
| **MIDI — Output** | Destination for MIDI emitted by the emulated device, or none. |
| **MIDI — MIDI inputs** | Choose the part groups, A–D, that each input plays; an input with none is closed. See **Live MIDI** above. |
| **MIDI — Enable Virtual Port** | Create the player's A–D MIDI IN/OUT endpoints on macOS and Linux. Windows needs an external loopback MIDI driver instead; give its input a part group here. |
| **Developer — Reload skin with F5** | Reload the current skin from disk with F5 while editing it. |
| **Developer — Enable RmlUi debugger** | Inspect UI elements and their styles during skin development. |

### Output limiter and resampling

The player's volume control ranges from 0 to 200% (unity at 100%). The optional limiter acts after that gain on both playback and recordings. It links left and right to preserve their balance, with immediate attack, a 100 ms release and a ceiling of 0.98, adding no latency. It limits **sample peaks**, not reconstructed inter-sample peaks, and cannot undo clipping inside the emulated device. Reduce volume if the output is being limited heavily.

The three resamplers trade CPU cost, latency and conversion quality:

- **MAME High Quality:** polyphase sinc filtering, strong rejection of unwanted frequencies and approximately 5 ms filter latency; the default for playback and the CLI's fixed choice.
- **Legacy:** the earlier libresample sinc converter, with lower computational cost than HQ in typical conversions.
- **MAME Lo-Fi:** inexpensive four-point interpolation with very low latency; useful when CPU time is tight, with reduced filtering quality.

See [Resampler Modes for TUS Plugins](https://theusualsuspects.io/2026/04/04/resampler-modes-for-tus-plugins.html) for the framework's technical comparison.

Software rendering provides a fallback for graphics-driver problems. If the UI cannot open, close the application and set `<VALUE name="forceSoftwareRenderer" val="1"/>` inside the config's existing properties list. The [software-renderer guide](https://theusualsuspects.io/2026/01/05/software-renderer-for-tus-plugins.html) explains the framework option; 88emuPlayer also exposes it directly in Settings → Interface.

## Analog output emulation (experimental)

For all of you that prefer having an even more accurate hardware emulation experience, we added some DSP models that simulate the original DAC and analog audio
output stages of some devices. Unless the CPU and sound chip digital emulation, we cannot make any claim regarding their accuracy, plus they make performance worse, they introduce latency and they can skew the phase. For these reasons, they are off by default.

Choose **Settings → Audio → Analog Output Emulation → Auto** to hear the selected board's modeled DAC and line-output circuit. **Off**, the default, uses the resampled digital output without that circuit. You can also try using the circuit emulation from another board if you wish.

The models were derived from measurements and schematics analysis. Conventional DACs hold each sample until the next one; this softens high frequencies and creates spectral **images**. For example, a 12 kHz tone from a 32 kHz DAC also produces an image at 20 kHz. The model runs at a higher rate so the output circuit can filter those images before conversion to your audio-device rate. The SC-8850 and SC-8820 instead model the DAC's internal interpolation filter as well as the analog circuit.

This is a normalized line-output model. It includes DAC word width, sample hold or interpolation, reconstruction filters and DC blocking. It does not simulate component aging, DAC nonlinearity/noise, headphone amplifiers, muting transients or the physical volume pot. The CM boards' PWM-driven volume VCA (the M5207L01 that master volume drives on the CM-32L, CM-32P and the new MT-32 board) is modelled on the board itself, whatever the analog setting, as is the LA boards' output wiring. The same nominal processing is applied to left and right, except on the CM-32L, CM-32P and CM-64, where the right channel carries the small trims measured on one CM-64; component mismatch beyond that and analog crosstalk are not modeled. “Imaging” here means frequency images, not stereo width.

The figures below are **calculated circuit responses**, not measurements of physical units - except that the CM-32L, CM-32P and CM-64 circuits were fitted to captures of a real CM-64 (see `tools/cm64_calibration` in the source tree) and are the ones the calculation is checked against. Frequency-response levels are relative to 1 kHz; image levels are relative to the tone producing the image. More negative values mean greater attenuation. Images above half your host sample rate are removed by the host resampler, subject to its filtering quality.

| Circuit | DAC / native word rate | 10 kHz | 14 kHz | Example image attenuation | Character |
| --- | --- | --- | --- | --- | --- |
| CM-32L | PCM54HP, 16-bit / 32 kHz | +1.7 dB | +0.8 dB | −13.8 dB at 20 kHz | Two peaking low-pass sections lift the treble past the hold droop, then the VCA and output stages; the MT-32 new board is the same circuit. |
| CM-32P | PCM56P, 16-bit / 32 kHz | −2.4 dB | −5.0 dB | −20.7 dB at 20 kHz | Fitted reconstruction filter; more damped than the CM-32L's, rolling off from the upper treble. |
| CM-64 | both boards' DACs / 32 kHz | +1.5 dB (LA), −2.4 dB (PCM) | +0.4 dB (LA), −5.0 dB (PCM) | −14.3 dB (LA), −20.7 dB (PCM) at 20 kHz | Each half through its own reconstruction filter, then the PCM board's mixer and output network. |
| SC-55 | µPD6376, 16-bit / 64 kHz | −0.6 dB | −1.2 dB | −18.6 dB at 52 kHz | Gentle treble loss; principal images lie above ordinary audio-output bandwidth. |
| SCC-1 | µPD6376, 16-bit / 64 kHz | −3.7 dB | −6.2 dB | −36.5 dB at 52 kHz | An in-band low-pass makes the card darker than the SC-55 module. |
| SC-55mkII | µPD63200, 18-bit / about 66.2 kHz | −0.5 dB | −1.0 dB | −17.4 dB at about 54 kHz | Gentle treble loss, with images well above the audible band. |
| SC-88 | PCM69AU, 18-bit / 32 kHz | −1.6 dB | −3.2 dB | −7.0 dB at 20 kHz | Little reconstruction filtering; relatively strong high-frequency images. |
| SC-88VL | µPD63200, 18-bit / 32 kHz | −2.0 dB | −4.1 dB | −9.1 dB at 20 kHz | A gentle low-pass softens the top and reduces images somewhat. |
| G-800 | PCM69AU, 18-bit / 32 kHz | −1.6 dB | −3.1 dB | −7.0 dB at 20 kHz | Close to the SC-88 output response. |
| SC-88Pro | PCM69AU, 18-bit / 32 kHz | −2.3 dB | −6.0 dB | −15.0 dB at 20 kHz | Stronger reconstruction filtering and pronounced upper-treble roll-off. |
| SC-8850 | AK4324, 24-bit / 32 kHz | −0.7 dB | −2.2 dB | Below −80 dB from 17.5 kHz | Interpolating DAC suppresses images; analog low-pass near 16 kHz. |
| SC-8820 | PCM1716, 24-bit / 32 kHz | −0.8 dB | −2.4 dB | Below −80 dB from 17.5 kHz | Interpolating DAC with a similar, slightly more damped output filter. |

**Auto mappings:** each named module uses its own circuit; XPGS uses G-800, VE-GS Pro uses SC-88Pro, and SCC-1A uses SCC-1. CM-300/SCC-1 uses the SC-55 module circuit by default; select **SCC-1** manually for the card. SC-155 uses the SC-55 circuit; SC-155mkII, SC-55st, SCB-55 and RLP-3237 use the SC-55mkII circuit. The MT-32 new board uses the CM-32L circuit, its own design; the MT-32 old board and the CM-32LN have not had their output stages read yet and stay digital under Auto. The CM-64 circuit only makes sense on the CM-64, where the two halves are filtered apart; picked for another board it applies the mixer and output stage alone.

## Hardware buttons and keyboard shortcuts

Open **Keyboard Shortcuts** from the context menu for the current model's bindings. Focus the hardware panel before using them. A held key is a held physical button; release it to release the button. You can hold multiple keys, or hold a key while clicking another panel button, to enter the firmware's button combinations and special menus.

These combinations work on the running hardware panel. Power-on combinations are made as on the hardware.

**Models with a standby.** Put the unit in standby with **Q**, hold the desired panel keys, then press **Q** again. The firmware reads the held keys as it wakes. For example, to initialize an SC-55mkII, hold **Y + U** (both INSTRUMENT buttons) while pressing **Q**, then press **W** (ALL) at "Init All, Sure?".

**Power supply.** The power supply switch also preserves held panel buttons across power-off/on. That switch is **Q** on the other models and **Shift+Q** on the standby models. Turn the power off, hold the desired panel keys, turn it on again, and keep holding the keys until the firmware responds. Use it for combinations the firmware reads only at a cold start, such as the SC-88Pro test mode below. When powering on with held buttons, automatic Factory Reset and Fast Boot are bypassed for that boot so they cannot consume or replace the chord.

Restart Device and model changes release held buttons. Special-menu combinations depend on the device and firmware revision.

### SC-55 / SC-88 / SC-88Pro panel family

Only controls physically present on the selected profile are active. SC-55-family panels omit the map, Preview, User Inst/Select and vibrato/EFX rows; panel-less profiles omit hardware buttons altogether.

| Keys | Hardware buttons |
| --- | --- |
| `Q` | POWER: standby / on (SC-55/SC-55mkII, SC-155/SC-155mkII, SC-88VL); power on/off (other models) |
| `Shift`+`Q` | Power supply off / on (SC-55 family, SC-88VL, SC-88Pro) |
| `W`, `E` | ALL, MUTE |
| `R` / `T` | PART left / right |
| `Y` / `U` | INSTRUMENT left / right |
| `P` / `[` | LEVEL left / right |
| `D` / `F` | PAN left / right |
| `G` / `H` | REVERB left / right |
| `J` / `K` | CHORUS left / right |
| `I` / `O` | KEY SHIFT left / right |
| `A` / `S` | MIDI CH left / right |
| `1`, `2` | SC-55 MAP, SC-88 MAP / EQ (model-dependent) |
| `3`, `4` | USER INST, SELECT |
| `Tab` | PREVIEW, where fitted |
| `Z` / `X` | VIB RATE / EFX TYPE left / right |
| `C` / `V` | VIB DEPTH / EFX PARAM left / right |
| `B` / `N` | VIB DELAY / EFX VALUE left / right |

For example, on SC-88Pro, hold **2** (SC-88 MAP) and press **I** or **O** (KEY SHIFT) to adjust **DELAY**. The bottom edit-row functions change with USER INST and SELECT, as on the hardware.

To enter the SC-88Pro startup test mode, press **Q** to switch the power off, hold **I + O** (both KEY SHIFT buttons), then press **Q** to switch it on. At the diagnostic prompt, press **Tab** (PREVIEW). Press **Q** to switch the power off when finished.

### SC-8850

| Keys | Hardware buttons |
| --- | --- |
| `Q` | Power on/off |
| `E`, `D`, `X` | EDIT, DRUM, EFFECTS |
| `←` / `→` | PART left / right |
| `V`, `N`, `I` | VARIATION, INSTRUMENT, INST MAP |
| `-` / `=` | DEC / INC |
| `Space` | VALUE encoder push |
| `Backspace`, `Return` | EXIT, ENTER |
| `Shift` | SHIFT |
| `S`, `M`, `P` | SOLO, MUTE, PREVIEW |
| `F1`–`F4` | Function buttons F1–F4 |

Hold **Shift** (either key), then press another key to send a hardware SHIFT combination. For example, SHIFT + PART left is **Shift + ←**, used by the firmware's initialization menu. INC is the `=` key, so it needs no Shift; on keyboards where `+` has a key of its own, that key works too. Menu actions can overwrite device settings just as they do on the unit.

### MT-32, CM-32L and CM-64

The MT-32's front panel has no faces in the skin yet, so its switches are keyboard-only. The same keys reach the CM-32L's and the CM-64's LA board, whose firmware scans the matrix although the case has no switches; only the power-on chords do anything there.

| Keys | Hardware buttons |
| --- | --- |
| `Q` | Power on/off |
| `1`–`5` | PART 1–5 |
| `R` | RHYTHM |
| `G`, `S` | SOUND GROUP, SOUND |
| `V`, `M` | VOLUME, MASTER VOLUME |
| `↑` / `↓` | VOLUME/VALUE knob up / down, a 32nd of its travel per press (MT-32 only) |

To enter the firmware's test mode, press **Q** to switch the power off, hold **3 + V** (PART 3 + VOLUME), then press **Q** to switch it on and keep holding until the screen changes. **4 + R + M** (PART 4 + RHYTHM + MASTER VOLUME) at power-on shows the firmware version instead. The CM-32P has no switches at all.

## Skins

88emuPlayer uses our **RmlUi** framework: RML markup for layout, RCSS styles, images/SVGs and shared custom controls. The [TUS skinning guide](https://theusualsuspects.io/docs/rmlui-skinning) describes the framework, its controls, styles and debugging tools. The player's hardware/playlist bindings are specific to 88emuPlayer; a skin for another TUS product needs adaptation.

1. In **Settings → Skins**, export the embedded skin and open the skins folder.
2. Copy the exported `88emuPlayer` folder to a new name before editing it.
3. Edit `emu88Player.rml`, its RCSS files and assets, retaining the IDs used by the hardware panel, playlist and settings controls.
4. Activate the new skin. Enable **Reload skin with F5** and, optionally, the **RmlUi debugger** in Developer settings.

Each skin lives directly under `skins/<name>/`, with its root `.rml` document and resources in that folder. Keep resource references as filenames. External resources override matching embedded resources; missing ones fall back to the bundled set. If a skin fails to load, the player returns to the embedded skin and reports the error.

## Command-line usage

Both programs accept `--option value` and `--option=value`. Quote paths and endpoint names containing spaces. Use `--` before an input filename beginning with `-`. Device IDs are listed in the hardware table and by `--list-devices`.

On macOS, run the GUI executable at `88emuPlayer.app/Contents/MacOS/88emuPlayer`; `88EmuCli` is a separate executable supplied with the application. The examples assume the executables are on your shell's path; otherwise use their full paths.

### Shared options

Explicit options override the selected config, followed by built-in defaults.

| Option | Meaning |
| --- | --- |
| `--help` | Print available options and exit. |
| `--list-devices` | Print listed device IDs and ROM availability, then exit. |
| `--rom-dir PATH` | Search only this folder, recursively. Default: the data folder above. |
| `--config PATH` | Use this XML settings file. The CLI reads an existing file without saving it. |
| `--device ID` | Select a model. Default: configured model, otherwise `sc88pro`. An explicitly requested model does not fall back when its ROMs are missing. |
| `--reset off\|gm\|gm2\|gs\|mt32` | Song-start reset. Default: configured choice, otherwise GS. On the MT-32 and CM devices every mode but `off` sends their own all parameters reset. |
| `--song-gap-ms N` | Pause between GUI playlist songs, 0–60,000 ms; default 1,000. Accepted by CLI but has no effect on its single-file render. |
| `--sample-rate HZ` | Requested output rate, 8,000–192,000 Hz. GUI hardware must support it; CLI uses the saved rate or 44,100 Hz by default. |
| `--gain N` | Linear gain: 0–2 in GUI, 0–4 in CLI. Default: saved gain or 1. |
| `--limiter on\|off` | Output sample-peak limiter. Default: saved setting or off. |
| `--factory-reset on\|off` | Factory initialization when loading a supported board. Default: saved setting or on. |
| `--fast-boot on\|off` | Run ten extra seconds of boot time before use. Default: saved setting or off. |
| `--pcm-card PATH` | CM-32P / CM-64 PCM card image for the card slot, up to 512 KiB; see the card instructions above. The player keeps it for the session without changing the saved choice. |

### Launching the player

```sh
88emuPlayer --rom-dir "/path/to/roms" --device sc88pro \
  --reset gs --virtual-ports on --play "first.mid" --playlist "second.g36"
```

Positional files and repeated `--playlist` arguments are appended in argument order. Without `--play` they are loaded without starting playback. `--play` starts the first entry after a five-second boot wait; use Fast Boot when you need the firmware fully past its intro first.

The standalone player restores its previous playlist when launched without input files. Playlist changes are saved automatically to `playlist/last-session.m3u8` in the 88emu data folder. Right-click the playlist to load or save an `.m3u`/`.m3u8` playlist; saved playlists use relative paths where possible. The Add button and playlist drop target also accept playlist files and append their entries. Supplying positional files or `--playlist` starts a new playlist and makes it the next restored playlist. Unlike entries of a restored playlist, a file named on the command line must be readable, or the player does not start.

When the player cannot start, it prints the reason to stderr. Opened from Finder, Explorer or a desktop launcher, where there is no terminal to read it in, it shows the reason in a message box instead.

| GUI-only option | Meaning |
| --- | --- |
| `--playlist PATH` | Add one song; repeat for more. |
| `--play` | Start the preloaded playlist after boot. |
| `--list-endpoints` | List audio backends/outputs and MIDI names/IDs without opening them. |
| `--audio-backend NAME` | Exact backend name from endpoint discovery. |
| `--audio-device NAME` | Exact output name within that backend, `none`, or `system` to follow the default on CoreAudio / Windows Audio. |
| `--buffer-size N` | Requested callback size in frames; must be supported by the device. |
| `--output-channels L,R` | Two distinct 1-based hardware channels, such as `3,4` or `4,3`. First is left, second is right. |
| `--midi-in ID_OR_NAME` | Enable an input for part group A; repeat for several. Replaces saved inputs. Use `none` alone to disable all. |
| `--midi-in-a` / `--midi-in-b` / `--midi-in-c` / `--midi-in-d` | Enable an input for the named part group; each is repeatable. The same input can be specified for several groups. `--midi-in-a` is equivalent to `--midi-in`. |
| `--midi-out ID_OR_NAME` | One output endpoint, or `none`. |
| `--virtual-ports on\|off` | Enable/disable virtual ports. Supported on macOS/Linux, unavailable on Windows. |
| `--virtual-port-name PREFIX` | Default `88emu`; produces `PREFIX MIDI IN A` through `D`, and corresponding `MIDI OUT` names. |
| `--save-settings` | Save launch overrides and subsequent session settings on normal exit. |

MIDI IDs take precedence over names; use an ID when names are duplicated. Explicit audio choices fail with a diagnostic if unavailable or unsupported. Changing to ASIO leaves audio closed until a driver is explicitly selected.

With launch overrides, the GUI uses a temporary config for the whole session; settings changes do not alter the source config unless `--save-settings` is supplied. `--config` alone selects a persistent alternative file, which may be new.

### Offline rendering with 88EmuCli

The CLI is useful for converting MIDI/Recomposer collections, making repeatable renders with different devices, and capturing songs without opening an audio device or routing virtual MIDI. It advances emulated time as quickly as the computer permits, rather than waiting for audio callbacks. Rendering speed depends on the model and computer; it is not guaranteed to exceed real time.

```sh
88EmuCli --rom-dir "/path/to/roms" --device sc88pro \
  --reset gs --sample-rate 48000 --bits 24 --limiter on \
  --output "rendered-song.wav" "song.mid"
```

| CLI-only option | Meaning |
| --- | --- |
| `--output PATH` | Required stereo WAV destination; parent directory must exist. Exactly one input is rendered. |
| `--bits 16\|24\|32` | 16/24-bit integer PCM or 32-bit floating point. Default 24. |
| `--boot-ms N` | Discard this much emulated boot audio before playback, after factory-reset/Fast Boot work. Default 5,000 ms. Increase for firmware needing more startup time. |
| `--tail-ms N` | Audio after the final MIDI event. Default 4,000 ms; up to 600,000 ms. |
| `--max-seconds N` | Optional duration cap, including reset settling and the tail. |
| `--overwrite` | Allow replacement of an existing output after rendering succeeds. |
| `--quiet` | Suppress progress; errors and clipping/custom-ROM warnings remain visible. |

The CLI uses **MAME High Quality** resampling and reads the saved analog-output choice. Boot audio is excluded from the WAV; song-reset settling is included. Without the limiter, integer WAV output clips above full scale; 32-bit float preserves overloads. Both report samples above full scale.

Output is written to a temporary file and published only after a successful render. Existing output is protected unless `--overwrite` is supplied. Exit codes are **0** for success, **2** for invalid arguments, **3** for input/config/ROM errors and **4** for rendering/output errors.

## Credits and attribution

Thanks to the people whose work made this possible:

- **nukeykt**, whose incredible work in [Nuked-SC55](https://github.com/nukeykt/Nuked-SC55) shed light on many of the algorithms used by those romplers,
  for the the original GP and LA32 chip emulator implementation and for the original SC-55 emulation (LCD mapping and board mapping).
- **Sergey V. Mikayev** and the [munt](https://github.com/munt/munt) project, for the emulator of the Boss reverb gate array, for the
  MT-32 ROM catalogue and for the knowledge of the MT-32 family that munt keeps.
- **mckuhei**, for the help finding many accuracy bugs and verifying the SC-8850 behavior.
- all the people and composers in the **DTM MIDI Central** community for the beta testing, advice and some incredible test MIDI files.
- **ValleyBell** for the help with MIDI formats decoding, CM-32P and CM-64 research.
- **MAME / mamedev**, for the original reference implementations of many CPUs and other devices.
- **superctr**, for the SC-8820 support and the intensive beta testing.
- **masanaohayashi** and **shingo45endo**, for the Recomposer support.
- **hackyourlife**, for the performance improvements in the descrambling algorithm.
- **InfoSecDJ** and **John McMaster**, for the incredibly high quality die shots.

## License and legal

88emuPlayer and 88EmuCli are free software: you can redistribute them and/or modify them under the terms of the GNU General Public License, version 3, as published by the Free Software Foundation. They are distributed in the hope that they will be useful, but **without any warranty**, without even the implied warranty of merchantability or fitness for a particular purpose. `LICENSE.md` contains the full license. The GPL entitles you to the complete corresponding source code.

Parts of 88emu are derived from other free software and keep their own licenses, which are compatible with the GPLv3 of the whole:

- The GP sound chip emulation, the SC-55 board mapping, its panel artwork and LCD font are derived from **Nuked-SC55**, Copyright (C) 2021, 2024 nukeykt, GPL-2.0-or-later.
- The LA32 sound chip emulation is derived from **Nuked-MT32**, Copyright (C) 2024, 2025 nukeykt, GPL-2.0-or-later.
- The Boss reverb gate array emulation is derived from Sergey V. Mikayev's **HG61H20R36F emulator** in munt, Copyright (C) 2013, 2014, LGPL-2.1-or-later.

It is the sole responsibility of the user to operate this emulator within the bounds of all applicable laws. Using it with ROM images you are not legally entitled to own is forbidden by copyright law. If you are not legally entitled to use it, please stop using it. This package contains no ROM images.

Roland, Sound Canvas, GS and the Roland product names in this document are trademarks of Roland Corporation. Yamaha and XG are trademarks of Yamaha Corporation. 88emuPlayer is an independent project and is not affiliated with, sponsored by or endorsed by Roland or Yamaha.
