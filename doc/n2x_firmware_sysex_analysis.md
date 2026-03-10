# Nord Lead 2x Firmware SysEx Analysis

## Background

The Nord Lead 2x (NodalRed2x) uses a Motorola MC68332 microcontroller (CPU32 core) alongside a DSP56362 for audio synthesis. This document presents the results of reverse-engineering the 68K firmware to understand its SysEx processing pipeline, identify undocumented commands, and find a solution for the Master Tune parameter bug (BUG-10010).

The analysis was performed by disassembling the firmware binary (`nord_lead_2x.bin`, 512 KB ROM) using the Capstone disassembly framework with correct relocation (firmware code is copied from ROM offset `$1001` to RAM at `$100000` during boot).

---

## Firmware Architecture

### Memory Map

| Region | Address Range | Description |
|--------|--------------|-------------|
| ROM | `$000000`-`$07FFFF` | 512 KB firmware ROM |
| RAM | `$100000`-`$13FFFF` | 256 KB working RAM (firmware copied here) |
| DSP | `$200008`/`$200010` | DSP56362 HDI-08 interface |
| Front Panel | `$202000`/`$202800` | Buttons, LEDs, encoders |
| Keyboard | `$203000` | Key scanner |
| QSM | `$FFFC00`-`$FFFC0F` | Queued Serial Module (MIDI UART) |

### Boot Sequence

1. Bootloader at ROM `$000000`-`$0FFF` reads length byte from ROM[`$1000`] (value `$60` = 24576 groups × 4 bytes = 96 KB).
2. Copies firmware from ROM[`$1001`..] to RAM[`$100000`..].
3. Jumps to entry point at `$1000C8`, which loads the global base register **A4 = `$110832`** from RAM[`$100008`].
4. All firmware code uses **A4-relative addressing** for function calls and data access.

### Key Global Data (A4-relative offsets)

| Offset | Type | Description |
|--------|------|-------------|
| `$032E` | word | MIDI event ring buffer count |
| `$0330` | word | MIDI event read index |
| `$0332` | word | MIDI event write index |
| `$0334` | 1024B | MIDI event ring buffer (128 × 8 bytes) |
| `$0A70` | word | Note event count |
| `$0A72` | word | Note event write index |
| `$0A7C` | 384B | Note event ring buffer (128 × 3 bytes) |
| `$0A6D` | byte | "Waiting for data byte 2" flag |
| `$0A6E` | byte | "In SysEx" flag |
| `$13FC` | byte | Running status byte |
| `$13FD` | byte | MIDI data byte 1 |
| `$13FE` | byte | MIDI data byte 2 |
| `$19BA` | 90B | Multi global parameters (A-D channel assignments, tune, etc.) |
| `$19ED` | byte | Device ID (protected from multi dump) |
| `$19EE` | byte | Global MIDI Channel (protected) |
| `$19F0` | byte | **Master Tune** (protected) |
| `$1A24` | long | Pointer to SysEx circular buffer |
| `$1A58` | long | Calculated DSP tune value |
| `$5DF4` | — | Single patch bank storage base |
| `$642E` | — | Multi patch bank storage base |

---

## MIDI Processing Pipeline

### Interrupt Level: SCI Receive

**Vector 15 (`$3C`) → `$100DBA`**

The SCI (Serial Communication Interface) interrupt handler receives one MIDI byte at a time:

```
$100DBA: Save D0-D2/A0-A1
$100DBE: Read SCSR ($FFFC0C)     ; Clear interrupt flag
$100DC4: Read SCDR ($FFFC0E)     ; Get received byte
$100DCA: Push byte as argument
$100DCC: JSR $100C8E             ; Call MIDI byte handler
$100DD0: Clean up stack
$100DD2: Restore registers
$100DD6: RTE
```

### MIDI Byte Handler (`$100C8E`)

This function implements a complete MIDI parser with running status support:

**Status byte (bit 7 set):**
- `$F8` (Timing Clock) → `$100B26`
- `$FA` (Start) → `$100B78`
- `$FB` (Continue) → `$100B78` (same handler as Start)
- Other System Real-Time → ignored
- `$F0` (SysEx Start) → set "in SysEx" flag, pass to SysEx byte handler
- `$F7` while in SysEx → pass to SysEx byte handler, clear SysEx flag
- Channel status → store as running status

**Data bytes (bit 7 clear):**
- If in SysEx mode → pass to SysEx byte handler (`$100B14`)
- If 2-byte message complete:
  - `$9x` Note On → store in note ring buffer at A4+`$0A7C`
  - `$8x` Note Off → `$100BAE`
  - `$Bx` Control Change → create type=1 event in MIDI ring buffer
  - `$Ex` Pitch Bend → `$100C38`
  - `$Cx` Program Change → create type=2 event in MIDI ring buffer
  - `$Dx` Channel Aftertouch → `$100C0A`

### SysEx Byte Handler (`$100B14` → `$10D0F8`)

Each SysEx byte (including `$F0` and `$F7`) is stored in a **4096-byte circular buffer** (pointed to by A4+`$1A24`):

```
Buffer structure at pointer:
  +$0000: 4096-byte circular data buffer
  +$1000: Write position (long)
  +$1004: Read position (long)
  +$1008: Complete message count (long)
  +$100C: Overflow count (long)
  +$1014: Overflow/error flag (byte)
  +$1016: Start position of current SysEx (long)
```

When `$F7` is received (SysEx end), a **type=3 event** is added to the MIDI event ring buffer, signaling the main loop that a complete SysEx message is ready.

### Main Loop Event Dispatch (`$1002A4`)

The main loop reads events from the MIDI ring buffer (blocking read with DSP processing during wait). Events are dispatched via a **14-entry jump table** at `$100372`:

| Type | Handler | Description |
|------|---------|-------------|
| 0 | → `$1002A8` | No event (loop) |
| 1 | → A4+`$7434` | Control Change |
| 2 | → A4+`$743C` | Program Change |
| **3** | → **`$101660`** | **SysEx message** |
| 4 | → A4+`$742C` | Unknown |
| 5 | → A4+`$741C` | Unknown |
| 6 | → A4+`$7424` | Unknown |
| 7 | → A4+`$759C` | Unknown (with flag=1) |
| 8 | → A4+`$7444` | Unknown |
| 9 | → `$103DE8` | Unknown |
| 10 | → A4+`$74B4` | Unknown |
| 11 | → A4+`$74C4` | Unknown |
| 12 | → complex | Multi-step operation |
| 13 | → return | Exit dispatch loop |

---

## SysEx Message Handler (`$101660`)

This is the core SysEx processor. It:
1. Reads the complete SysEx message from the circular buffer (up to 2200 bytes)
2. Validates the header: `F0 33 [DeviceID] 04 [MsgType] [MsgSpec] ... F7`
3. Verifies manufacturer ID (`$33` = Clavia), device ID (A4+`$19ED`), and product ID (`$04` = N2X)
4. Dispatches based on MsgType

### Complete SysEx Message Type Map

```
MsgType Range    | Description                     | MsgSpec Constraint
-----------------|---------------------------------|-------------------
0x00 (0)         | Dump to bank slot               | 0-19 (bank program)
0x01-0x0A (1-10) | Dump to part/edit buffer        | < 109
0x0E-0x18 (14-24)| Single request                  | ≤3 for type 14
0x1C (28)        | Refresh CCs for part            | 0-3 (part number)
0x1E (30)        | Multi dump to edit buffer       | Must be 0
0x1F-0x22 (31-34)| Multi dump to bank A-D          | < 100
0x28-0x2C (40-44)| Multi request                   | Must be 0 for type 40
0x33-0x36 (51-54)| Part-specific dump (parts 0-3)  | < 109
0x3D (61)        | Special multi dump to edit      | < 100
```

### Undocumented Types

The following message types are **not documented** in the official MIDI Implementation:

- **Types 5-10 (`$05`-`$0A`):** Additional dump targets for the current part/edit buffer. These follow the same code path as types 1-4 but the firmware accepts them. The exact semantics are unclear but they likely map to additional bank slots or multi-mode configurations.

- **Types 51-54 (`$33`-`$36`):** Part-specific dump commands. These bypass the firmware's "current part" selection and directly address parts 0-3 (type 51 = part 0, type 54 = part 3). They support both single-sized (MsgSpec < 99, 66 bytes decoded) and multi-sized (MsgSpec ≥ 99, 528 bytes decoded) data.

- **Type 61 (`$3D`):** Special multi dump that applies to the edit buffer via a different handler than type 30. It calls `$7524(A4)` instead of `$750C(A4)`, suggesting a different application strategy. This type may be used during bulk transfers or factory reset operations.

- **Type 28 (`$1C`):** A "refresh CC" command. When received with MsgSpec 0-3, it causes the firmware to read the current single patch for the specified part and **send CC messages** for key parameters on that part's MIDI channel. The CCs sent include: #5 (Portamento Time), #7 (Volume), #8 (Balance), #15-#21 (various controls). This is useful for synchronization.

### Nibble Encoding (`$1010A8`)

All SysEx data uses nibble encoding. Each parameter byte is split into two SysEx bytes:
- First byte = low nibble (bits 0-3)
- Second byte = high nibble (bits 4-7)

Decoded value = `first_byte + (second_byte << 4)`

This limits each parameter to the 0-255 range while keeping all SysEx data bytes below 0x80.

---

## Master Tune Analysis

### The Bug (BUG-10010)

When changing Master Tune through the emulator's UI, the parameter value updates in the State object, but the tuning of the emulated synthesizer doesn't change.

### Root Cause: System Parameter Protection

The multi dump apply function at `$10C12C` contains a deliberate protection mechanism for "system" parameters. Before copying the 90 bytes of multi global data to RAM, it **saves** 8 specific system bytes. After the copy, it **restores** them, effectively ignoring those values from the incoming multi dump.

**Protected parameters (saved before multi copy, restored after):**

| RAM Offset | MultiParam | Parameter Name |
|-----------|------------|----------------|
| A4+`$19ED` | 315 | GlobalMidiChannel |
| A4+`$19EE` | 316 | MidiProgramChange |
| A4+`$19EF` | 317 | MidiControl |
| **A4+`$19F0`** | **318** | **MasterTune** |
| A4+`$19F1` | 319 | PedalType |
| A4+`$19F2` | 320 | LocalControl |
| A4+`$19F3` | 321 | KeyboardOctaveShift |
| A4+`$19F5` | 323 | ArpMidiOut |

Note: A4+`$19F4` (MultiParam 322 = SelectedChannel) is **not** protected — it's a volatile UI state that can change with multi loads.

This protection is intentional on real hardware: these are persistent system settings stored in EEPROM, configured from the System menu, and should not change when loading a new Multi performance. Multi dumps are for musical presets, not device configuration.

### Master Tune → DSP Path

The front panel handler at `$109800` demonstrates the complete path:

```
1. Read current value:     move.b $19F0(A4), d0     ; -99 to +99
2. Adjust with encoder:    jsr $75E4(A4)             ; Clamp to range
3. Write back:             move.b d0, $19F0(A4)
4. Calculate DSP value:
     d0 = value * 128
     d0 = d0 / 100
     d0 = d0 + 0x18AE                                ; 6318 = center frequency
5. Store DSP value:        move.l d0, $1A58(A4)
```

**Formula:** `DSP_value = ((master_tune * 128) / 100) + 6318`

| Master Tune | DSP Value |
|------------|-----------|
| -99 | 6192 |
| 0 | 6318 |
| +99 | 6444 |

The DSP value at A4+`$1A58` is read in **4 places** in the firmware (one per part), where it is added to the note frequency calculation via `add.l $1A58(A4), Dx` instructions at:
- `$1050EE` (Part 0)
- `$1053A0` (Part 1)
- `$1068AC` (Part 2)
- `$106BE0` (Part 3)

The same initialization calculation exists at `$10429C`, executed during boot.

### Why Multi Dumps Fail for Master Tune

The data flow in the emulator is:

```
UI changes Master Tune
  → State::changeMultiParameter(318, value)
    → Creates full multi dump SysEx (type 0x1E / 30)
    → State::receive() stores value in software state ← this works
    → State::send() forwards SysEx to firmware
      → Firmware receives multi dump type 0x1E
        → Decodes 354 bytes of multi data
        → SAVES A4+$19F0 (Master Tune) ← protection!
        → Copies 90 global bytes to A4+$19BA
        → RESTORES A4+$19F0 ← value from dump is discarded!
        → Master Tune RAM unchanged
        → DSP value at A4+$1A58 unchanged
        → No tuning change audible
```

**There is no SysEx command in the original firmware that can modify Master Tune.** On real hardware, it can only be changed from the System menu using the front panel encoder.

---

## Recommended Solutions for the Emulator

### Option 1: Direct MCU Memory Write (Recommended)

After the firmware processes a multi dump, write directly to the emulated MCU's RAM:

1. Write the Master Tune byte value to the MCU address corresponding to A4+`$19F0`
2. Calculate the DSP value using the formula: `((value * 128) / 100) + 6318`
3. Write the 32-bit DSP value to MCU address corresponding to A4+`$1A58`

This bypasses the firmware's protection entirely. Since we control the emulated hardware, this is clean and safe. The approach should apply to **all 8 protected parameters** if any others need SysEx-based modification.

**MCU addresses (runtime):**
- Master Tune byte: `A4 + $19F0` = `$110832 + $19F0` = `$112222`
- DSP tune value: `A4 + $1A58` = `$110832 + $1A58` = `$11228A`

### Option 2: Custom Emulator SysEx Command

Add a new emulator-specific SysEx command (similar to existing `EmuSetPotPosition`/`EmuGetPotsPosition`/`EmuSetPartCC`) that writes directly to MCU RAM and recalculates the DSP value.

### Option 3: Simulate Front Panel Input

Inject synthetic front panel events (encoder rotations) to trigger the firmware's own Master Tune handler at `$109800`. This is the most "authentic" approach but requires understanding the front panel event queue.

### Option 4: Patch the Firmware Protection

Modify the multi-apply function to NOT protect A4+`$19F0`. This changes the firmware's original behavior and might cause unexpected side effects (e.g., loading a Multi from bank might overwrite the user's device ID or MIDI channel configuration).

---

## Additional Findings

### CC Handler

CC messages create a type=1 event in the MIDI ring buffer with the following structure:

```
Offset 0: word  type (1)
Offset 2: byte  MIDI channel (0-15)
Offset 3: byte  CC number (0-127)
Offset 4: byte  CC value (0-127)
```

There is **no CC mapping for Master Tune**. The firmware processes CCs through a separate dispatch path (A4+`$7434` → CC processing functions). Adding a CC mapping would require firmware modification.

### Note Ring Buffer

Note On events are stored directly in a 128-entry ring buffer at A4+`$0A7C` (bypassing the MIDI event ring buffer for lower latency):

```
A4+$0A7C + index:       note number
A4+$0A7C + index + 128: velocity
A4+$0A7C + index + 256: MIDI channel
```

### SysEx Buffer Limits

The SysEx circular buffer is 4096 bytes. Messages exceeding this trigger an overflow flag and are discarded. The maximum decoded multi size is 354 bytes (708 bytes nibble-encoded + 7 header/footer = 715 bytes), well within this limit.

### QSM Configuration

- SCCR0 at `$FFFC08`: baud divisor = `$11` (31.25 kBaud MIDI standard)
- SCCR1 at `$FFFC0A`: value `$100C` = TX enable + RX enable + RX interrupt enable
- SCI interrupt is Vector 15 (`$3C`)
