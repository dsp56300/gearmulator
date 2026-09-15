# SC-88 Pro voice retirement

Voice retirement is enabled for the verified VE-GSPro A and SC-88Pro 1.02
control ROMs. Other ROMs retain the existing behavior. No firmware bytes are
included in this change.

| ROM | MD5 | EG base | Release flag base | Allocation base |
| --- | --- | --- | --- | --- |
| VE-GSPro A | `784b3ea762b5f96cabdceb33d121d5e4` | `41CA` | `3D4A` | `E693` |
| SC-88Pro 1.02 | `9d4c2f123b4451d8ee75c3b982760f28` | `420A` | `3D8A` | `E695` |

SC-88Pro 1.02 routines `B088`/`B08E` finish the release and `74E4` marks a
completed allocation free. The following routine addresses describe VE-GSPro A.

The H8 writes the following per-slot SRAM fields (`v` is the XP slot index):

| Address | Meaning | Firmware routine |
| --- | --- | --- |
| `C0:41CA + 2*v` | Software EG value | `00:B03E` clears it at release completion |
| `C0:3D4A + 2*v`, bit 7 | Software release computation | `00:B044` clears the flag after the EG write |
| `C0:E693 + 2*v` | Allocation state | `00:7C2D` writes `01`; `00:7480` and `00:936B` write `FF` when freed |

The completion queue can free a slot without zeroing its old EG value.
Both completion paths are observed at SRAM writes. Pending retirements are
consumed every 128 samples (4 ms at 32 kHz). New allocation, envelope and
explicit XP reset writes cancel obsolete pending events.

Retirement parks waveform/filter processing without changing the host's
release-mask shadow. Unrelated mask reads cannot relaunch retired slots.
Explicit host reset/release still launches a new voice normally.

A retired slot continues servicing ramp and mute requests until quiescent.
Sleeping requires acknowledged destinations, published gain, and ramps that
are held or at their targets without a deliverable terminal request. Dynamic
trunk curves remain active unless held. The visible playback counter retains
its phase while asleep. Voice-register writes wake the affected slot, including
writes through the indirect window; IRQ-mask writes wake all retired slots.
This preserves the completion IRQs the H8 may request before reusing a slot.

## Validation

`sc88VoiceRelease` runs ROM-free XP lifecycle and wakeup tests through CTest.
The optional integration tests require the user's own ROM directory:

```sh
sc88VoiceRelease_test /path/to/roms
```

They cover held/released notes, pedal holds, zero volume/expression, two MIDI
ports, and a recorded keyboard sequence that exhausts the voice allocator.
Each subsequent attack is compared against retirement disabled. Set
`EMU88_IDLE_DIAGNOSTICS=1` to print a fresh/post-stress render-time comparison.
Timing is diagnostic; audio and lifecycle assertions determine pass/fail.
