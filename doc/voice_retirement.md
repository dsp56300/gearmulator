# SC-family voice retirement

The board firmware determines whether a voice has finished. Output amplitude,
channel volume and expression are not used as retirement conditions. Unknown
control-ROM hashes keep the original behavior.

## SC-88 and SC-88VL

Both use the XP retirement path described in `sc88pro_voice_retirement.md`.
The H8 writes the software EG to zero before clearing the release flag, so the
observer latches completion on the EG low-byte write. Allocation-state writes
also cover firmware completion paths that leave the old EG value in RAM.

| Model | Control ROM MD5 | EG base | Release flag base | Allocation base |
| --- | --- | --- | --- | --- |
| SC-88 | `0ac771782ea58a53af590ebdf140d517` | `405A` | `3BDA` | `F609` |
| SC-88VL | `25e016e93c8a44ba3c35584462b56d72` | `40D6` | `3C56` | `F61D` |

Addresses are offsets in page-08 SRAM, with two bytes per XP slot. Release is
bit 7; allocation `FF` means free. SC-88 routines `7228`/`722E` and SC-88VL
`7396`/`739C` finish the software release. SC-88 `4B15` and SC-88VL `4BFD`
mark a completed allocation free. Retirement runs every 128 samples (4 ms).
A new EG, allocation or explicit XP reset cancels a pending retirement.

## SC-55mk2

The supported internal/program ROM hashes are
`4ca058f7db05f51e97bb30a162e9610a` and `63b24c7193ce34afefce9cec32ac39f0`.
The H8 voice table at `64D6` points to 28 records beginning at `ADAE`, with a
`12A`-byte stride. The first word is the amplitude-envelope state, and `+1C`
is its software accumulator. State `0C` is release; routine `3095` writes
state `16` after the GP reports completion. The accumulator can still contain
a fractional remainder, so retirement requires the observed release followed
by this firmware completion state rather than a numerical volume threshold.

Every 256 samples (about 3.87 ms), completed slots enter the GP's existing
inactive-voice path. A separate retirement mask preserves the host-visible
mask registers. An explicit host key-off clears retirement so a subsequent
key-on starts normally; unrelated mask writes cannot restart a retired slot.
The effects section and inactive-voice cutoff tracking continue unchanged.

All Sound Off can leave firmware state `12` or `14` waiting for completion,
including with optimization disabled. Such slots retain the original processing:
this observer does not treat a forced-stop request as release completion.

## SC-8850

All three ROM hashes must match the supported set:

- CPU: `efe1ffb0ccbe1b2ec454692c522494fc`
- Program: `554d5997dcd9ce6fa0777092ff48f6fa`
- Data: `06eee65647b66109efb01eabd6d71248`

The SH-2 manages 128 records at `01022024`, with a `1E4`-byte stride. Byte 0
identifies the slot. Routine `6604` marks byte 1 free and `660C` clears the
active byte at `+148`. Retirement requires the slot identity, free flag and
inactive flag to agree. This also covers All Sound Off, which can free a slot
without setting the normal release flag at `+149`. The firmware may stop updating an envelope with a
nonzero accumulator, so allocator completion is authoritative.

Routine `F048` maps slots 0-63 to XP1 (`A80000`) and 64-127 to XP0 (`A00000`).
The board checks completion every 128 samples (4 ms), then uses the same XP
retirement and interrupt-preservation path as SC-88Pro.

## Validation

`voiceRelease` runs portable GP retirement-mask tests through CTest. Optional
integration checks use the user's ROM directory and compare against retirement
disabled:

```sh
voiceRelease_test /path/to/roms 88
voiceRelease_test /path/to/roms 88vl
voiceRelease_test /path/to/roms 55
voiceRelease_test /path/to/roms 8850
sc88VoiceRelease_test /path/to/roms
```

SC-88Pro covers both the VE-GSPro A and hardware 1.02 control ROMs, as detailed
in `sc88pro_voice_retirement.md`.

They cover held notes, zero volume/expression, sustain, sostenuto, string tails,
All Sound Off silence and subsequent reuse, every MIDI port,
1,324 recorded keyboard events, and 80 post-stress attacks per added model.
SC-8850 must exercise all 128 voices, including XP0. Set
`EMU88_IDLE_DIAGNOSTICS=1` for fresh/post-stress render timings. ROMs and local
RAM/disassembly probes are not included.
