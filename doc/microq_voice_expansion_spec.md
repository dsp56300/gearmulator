# microQ Voice Expansion — Technical Specification

## Overview

The Waldorf microQ supports voice expansion via up to 3 DSP56362 processors (1 main + 2 expansion boards). The 68k microcontroller (MC68331) manages boot, synchronization, and runtime communication with all DSPs. This document details the complete boot and ESAI sync protocol as reverse-engineered from the microQ firmware.

## Hardware Architecture

### DSP Addressing (HDI08)

| DSP   | Role        | HDI08 Base Address | Array Index |
|-------|-------------|-------------------|-------------|
| DSP A | Main        | `$FD000`          | 0           |
| DSP B | Expansion 1 | `$FB000`          | 1           |
| DSP C | Expansion 2 | `$FC000`          | 2           |

### Expansion Detection

The 68k firmware at `$89D46` probes expansion boards by writing to the IVR (Interrupt Vector Register) at each HDI08 address and reading it back. If the readback matches, the expansion board is present. Detection flow:

1. Write test value to `$FB001` (DSP B IVR)
2. Read back — if match → DSP B present
3. Write test value to `$FC001` (DSP C IVR)
4. Read back — if match → DSP C present
5. Branch to 1-DSP, 2-DSP, or 3-DSP boot path

## Boot Sequence

### Phase 1: DspBoot (Hardware Bootstrap)

For each DSP, the 68k uses the DSP56362's hardware bootstrap mechanism:
- Sends 181 words via HDI08: `[length=$B3=179] [address=$0700] [179 data words]`
- DSP hardware loads code at P:$0700 and begins execution

### Phase 2: Bootloader at P:$0700

The bootloader performs initialization then enters a command handler:

1. **Register Init**: Sets PCTL (PLL), AAR0-3 (address attribute), BCR (bus control)
2. **Memory Clear**: Zeros P:$0000-$05FF (1536 words) and L:$0000-$15FF (5632 words)
3. **Init Subroutine**: Calls $072C for AAR decode / memory protection setup
4. **Command Handler Copy**: Copies 256 words from P:$0764 → P:$13B1
5. **Jump to Handler**: `jmp $13B1`

### Phase 3: Command Handler at P:$13B1

Waits for HDI08 data and dispatches commands:

| Command | Function         | Data Format                        |
|---------|------------------|------------------------------------|
| 0       | Write P memory   | `[count] [address] [count × data]` |
| 1       | Write X memory   | `[count] [address] [count × data]` |
| 2       | Write Y memory   | `[count] [address] [count × data]` |
| 3       | Write X+Y paired | `[count] [address] [count × {x,y}]`|
| Other   | Start firmware   | `jmp $000100`                      |

Each command echoes the command word and parameters back via HOTX before processing.

### Phase 4: Firmware Loading

The 68k sends firmware in two stages:

#### Simultaneous Block (~11050 words)
- Sent to **all 3 DSPs simultaneously** via the `$89EF0` subroutine
- Contains the complete firmware code and data
- Identical for all DSPs
- Includes ESAI init template at P:$131A and sync loop at P:$1375

#### Individual Blocks (per-DSP)
- Sent to **each DSP separately** via the `$89F36` subroutine
- **Patches P:$131A onwards** with DSP-specific ESAI configuration and sync code
- Different size per DSP: A=124 words, B=115 words, C=109 words
- This is the critical differentiation that gives each DSP its unique role

### Phase 5: Firmware Start Trigger

The 68k sends `$BE00` to each DSP's command handler. Since `$BE00` is not 0/1/2/3, the handler executes `jmp $000100`, starting the firmware.

**Important**: The reset vector at P:$000000 points to `jmp $001306` which takes a shortcut path that **skips ESAI initialization**. The correct entry point at `$000100` includes the full init chain:

```
$000100 → jsr $000CB0 (voice init) → jsr $0012CF → jsr $00096C (memory init)
       → jsr $00131A (ESAI/DMA init — patched by individual blocks!)
```

## ESAI Sync Protocol

### Role Assignment

The individual blocks patch the ESAI init code at P:$131A to assign each DSP a specific role in the sync protocol:

### DSP A — Clock Master + Sync Slave

**ESAI Configuration:**
- TCCR = `$640305`: **TFSD + TCKD set** → generates TX clock and frame sync (bus master)
- RCCR = `$403E40`: RFSD set → RX frame sync output
- TCR = `$7D07`: TE0 + TE1 + TE2 enabled (3 transmitters)
- RCR = `$7942`: RE1 enabled (receives on SDI1)
- RSMA = `$FFFF`, RSMB = `$F`: 20 RX slots enabled
- **No TSMA/TSMB set** (TX slot masks remain at reset default = 0)
- PCRC = `$EFB`: SDO0-2, SDI0-1, clocks as ESAI; pin 2 = GPIO output HIGH
- PDRC = `$4`: pin 2 HIGH (drives SDO2/SDI2 as GPIO)

**DMA Channels (pre-configured, enabled after sync):**

| Channel | Source      | Destination         | Purpose              |
|---------|-------------|---------------------|----------------------|
| DMA 0   | X:$01C0    | M_TX0 (`$FFFFA0`)   | Audio → ESAI TX0     |
| DMA 1   | X:$0200    | M_TX1 (`$FFFFA1`)   | Audio → ESAI TX1     |
| DMA 2   | X:$0240    | M_TX2 (`$FFFFA2`)   | Audio → ESAI TX2     |
| DMA 4   | M_RX1 (`$FFFFA9`) | X:$0EC0       | ESAI RX1 → Audio buf |

**Sync Loop (P:$1375-$1380):**
```asm
001375: clr a    #>$654300, x0       ; a=0, x0=$654300
001377: brclr M_RDF, M_SAISR, $1377  ; wait for ESAI RX data
001379: movep M_RX1, b               ; read RX1 → b
00137a: brclr M_TDE, M_SAISR, $137f  ; skip TX if not ready
00137c: movep a, M_TX0               ; write 0 to TX0
00137d: movep a, M_TX1               ; write 0 to TX1
00137e: movep a, M_TX2               ; write 0 to TX2
00137f: cmp   x0, b                  ; b == $654300?
001380: bne   $1377                  ; loop if not
```

**Post-Sync:**
- Enables DMA channels 0-4 (DCR0=`$ac6600`, DCR4=`$ee5e40`)
- Sends `$654300` to 68k via `movep x0, M_HOTX`
- Waits for HF0 from host, then returns to main loop

---

### DSP B — Sync Master + Clock Slave

**ESAI Configuration:**
- TCCR = `$3E40`: **no TFSD/TCKD** → clock slave (receives clock from DSP A)
- RCCR = `$040305`: RDC=1 (2 words/frame)
- TCR = `$7942`: **TE1 only** (transmits on SDO1)
- RCR = `$7D01`: **RE0 only** (receives on SDI0)
- **TSMA = `$FFFF`, TSMB = `$F`**: all 20 TX slots enabled
- PCRC initial = `$10`: only pin 4 as ESAI (for clock detect)
- PCRC final = `$FFF`: all pins as ESAI
- PRRC = `$45B`: pins 0,1,3,4,6 as outputs

**DMA Channels:**

| Channel | Source      | Destination         | Purpose                    |
|---------|-------------|---------------------|----------------------------|
| DMA 0   | X:$0EC0    | M_TX1 (`$FFFFA1`)   | **Sync buffer → ESAI TX1** |
| DMA 4   | M_RX0 (`$FFFFA8`) | X:$1100       | ESAI RX0 → buffer          |

**Sync Sequence:**
```asm
; Phase 1: Wait for clock from DSP A
001348: jclr #4, M_PDRC, $1348       ; wait for pin 4 HIGH (ESAI clock present)

; Phase 2: Reconfigure ports and enable ESAI
00134a: movep a, M_TX1               ; prime TX1 with zero
00134c: movep #>$fff, M_PCRC         ; switch all pins to ESAI mode

; Phase 3: Clear X memory buffer and set magic value
001360: clr a  #>$ec0, r0            ; a=0, r0=$0EC0
001364: rep #<$27f                   ; repeat 639 times
001365: add x1,b  a,x:(r0)+          ; fill X:$0EC0-$113E with zeros
001367: clr a  #>$654300, x0         ; x0 = magic value
001369: move x0, x:>$113f            ; X:$113F = $654300 (last word of buffer)

; Phase 4: Enable DMA to transmit buffer → ESAI TX1
00136b: movep #>$ee6600, M_DCR0      ; enable DMA channel 0
00136d: jclr #2, M_DSR0, $136d       ; wait for DMA completion

; Phase 5: Enable DMA channel 4, wait for HF0
00136f: movep #>$ae5e40, M_DCR4      ; enable DMA channel 4
001371: move x0, x:>$113f            ; keep $654300 in buffer
001373: brclr HF0, M_HSR, $1371      ; loop until HF0 received
001375: move a, x:>$113f             ; clear magic value (a=0)
```

**Key Mechanism**: DMA Channel 0 automatically transfers the 640-word buffer (639 zeros + `$654300`) to ESAI TX1. DSP B does NOT manually write `$654300` to any ESAI TX register — DMA does it.

---

### DSP C — Sync Slave + Clock Slave

**ESAI Configuration:**
- TCCR = `$3E40`: clock slave
- RCCR = `$3E40`: RDC=$1F (32 words/frame)
- TCR = `$7942`: **TE1 only** (transmits on SDO1)
- RCR = `$7942`: **RE1 only** (receives on SDI1)
- **TSMA = `$FFFF`, TSMB = `$F`**: all TX slots enabled
- **RSMA = `$FFFF`, RSMB = `$F`**: all RX slots enabled
- SAICR = `$40`: bit 6 set (synchronous mode — TX clock/sync derived from RX)
- IPRC = `$303000`: both ESAI0 and ESAI1 interrupts at priority level 3

**DMA Channels (enabled after sync):**

| Channel | Source      | Destination         | Purpose                  |
|---------|-------------|---------------------|--------------------------|
| DMA 0   | X:$0EC0    | M_TX1 (`$FFFFA1`)   | Audio relay → ESAI TX1   |
| DMA 4   | M_RX1 (`$FFFFA9`) | X:$0EC0       | ESAI RX1 → Audio buffer  |

Note: DMA 0 and DMA 4 share the same X:$0EC0 buffer, creating a **loopback relay** (RX1 → buffer → TX1).

**Sync Loop (P:$136A-$1371):**
```asm
00134a: jclr #4, M_PDRC, $134a       ; wait for pin 4 HIGH (clock detect)
001350: clr a                        ; a = 0
001363: clr a  #>$654300, x0         ; x0 = magic value
001367: rep #<$280                   ; clear 640 words at X:$0EC0
001368: move a, x:(r0)+
001369: movep M_RX1, b               ; drain RX1

00136a: jclr M_TDE, M_SAISR, $136a   ; wait for TX ready
00136c: movep a, M_TX1               ; write 0 to TX1
00136d: jclr M_RDF, M_SAISR, $136d   ; wait for RX data
00136f: movep M_RX1, b               ; read RX1
001370: cmp x0, b                    ; b == $654300?
001371: bne $136c                    ; loop if not
```

**Post-Sync:**
- Enables DMA0 (DCR0=`$ee6600`) and DMA4 (DCR4=`$ec5e40`) for loopback relay
- Does **NOT** send `$654300` to 68k via HDI08
- Waits for HF0, then returns

## ESAI Signal Routing

### Physical Bus Topology

Based on the firmware configuration, the DSPs are wired as follows:

```
                    ESAI Clock Bus (SCKT/FST)
    DSP A (master) ─────────────────────────────→ DSP B, DSP C (slaves)

                    ESAI Data Bus
    DSP B ──SDO1/TX1──→ DSP A SDI1/RX1    (sync: $654300 via DMA)
    DSP B ──SDO1/TX1──→ DSP C SDI1/RX1    (sync: $654300 via DMA)

                    GPIO Sync (Port C Pin 4)
    DSP A clock ───→ DSP B PDRC bit 4     (clock-present detect)
    DSP A clock ───→ DSP C PDRC bit 4     (clock-present detect)
```

### Pin Assignments (Port C)

| Pin | Signal      | DSP A       | DSP B          | DSP C          |
|-----|-------------|-------------|----------------|----------------|
| 0   | SDO0        | ESAI out    | ESAI out       | ESAI (input)   |
| 1   | SDO1        | ESAI out    | ESAI out (TX1) | ESAI (input)   |
| 2   | SDO2/SDI2   | GPIO HIGH   | ESAI           | ESAI           |
| 3   | SDO3/SDI1   | ESAI        | ESAI out       | ESAI out       |
| 4   | SDO4/SDI0   | ESAI        | ESAI (clock detect) | ESAI (clock detect) |
| 5   | SDO5/SDI3   | ESAI        | ESAI           | ESAI           |
| 6   | SCKT        | ESAI (clk out) | ESAI (clk out) | ESAI (clk in) |
| 7   | FST         | ESAI (sync out) | ESAI (input) | ESAI (sync out) |
| 8   | HCKT        | GPIO LOW    | ESAI           | ESAI           |
| 9   | SCKR        | ESAI        | ESAI           | ESAI           |
| 10  | FSR         | ESAI        | ESAI           | ESAI (output)  |
| 11  | HCKR        | ESAI        | ESAI           | ESAI           |

## Complete Sync Timeline

```
Time ─────────────────────────────────────────────────────────────────→

68k:  [DspBoot A,B,C] [Simultaneous Block] [Individual Blocks] [Send $BE00]
         │                                                         │
DSP A:   │ bootloader → cmd handler → load firmware ──────→ jmp $100
         │                                                    │
         │                                            ESAI init (clock master)
         │                                            Enable TE0+TE1+TE2
         │                                            ┌─ Sync loop: write 0s ──┐
         │                                            │  to TX, read RX1,      │
         │                                            │  wait for $654300      │
         │                                            └────────────────────────┘
         │                                                         ▲
DSP B:   │ bootloader → cmd handler → load firmware ──────→ jmp $100
         │                                                    │
         │                                            ESAI init (clock slave)
         │                                            Wait PDRC bit 4 (clock)──┐
         │                                            Fill buffer, x:$113F=$654300
         │                                            Enable DMA0 → TX1 ───────┘
         │                                            DMA sends: 0,0,...,$654300
         │                                                         │
DSP C:   │ bootloader → cmd handler → load firmware ──────→ jmp $100
         │                                                    │
         │                                            ESAI init (clock slave)
         │                                            Wait PDRC bit 4 (clock)──┐
         │                                            Sync loop: write 0s      │
         │                                            to TX1, read RX1,        │
         │                                            wait for $654300 ────────┘
         │
         │  ◄── DSP A receives $654300 on RX1 ──→ sends $654300 to 68k via HOTX
         │  ◄── 68k reads $654300 from DSP A
         │  ◄── 68k sets HF0 on all DSPs
         │
All DSPs: HF0 received → rts → enter main audio loop
```

## Emulator Implementation Requirements

### 1. ESAI Inter-DSP Routing

The emulator must route ESAI TX output from one DSP to ESAI RX input of other DSPs:

- **DSP B TX1 → DSP A RX1**: sync magic value broadcast
- **DSP B TX1 → DSP C RX1**: sync magic value broadcast
- Post-sync audio routing TBD (requires further analysis of main loop)

Implementation approach: Similar to XT voice expansion ESSI1 ring routing — use TX frame callbacks to copy TX output frames to RX input of connected DSPs.

### 2. GPIO Clock Detect (PDRC Bit 4)

DSP B and DSP C poll PDRC bit 4 waiting for the ESAI clock to appear. In the emulator:

- When DSP A enables ESAI transmitters (TE0/TE1/TE2 in TCR), set a shared flag
- DSP B and DSP C's PDRC bit 4 reads should return this flag
- Alternative: trigger on first ESAI frame output from DSP A

### 3. ESAI Frame Pumping During Sync

During the sync phase, DSP threads are blocked in polling loops. The emulator must actively pump ESAI frames between DSPs so that:

- DSP B's DMA can transfer data from the buffer to TX1
- The TX1 output reaches DSP A's and DSP C's RX1 inputs
- DSP A and DSP C can read the received data

This is analogous to the XT `initVoiceExpansion()` boot pump loop.

### 4. DMA ↔ ESAI Integration

The DMA engine must properly handle writes to ESAI TX registers (M_TX0-TX5 at `$FFFFA0-$FFFFA5`). When DMA writes to M_TX1, this should:

1. Write the value to the ESAI TX1 register
2. Trigger ESAI frame assembly when all enabled TX slots have data
3. Generate the TX output frame for routing to other DSPs

### 5. HF0 Host Flag

After the 68k receives `$654300` from DSP A via HDI08, it must set HF0 on all 3 DSPs to release them from their wait loops:

- DSP A waits at `$1392`: `brclr HF0, M_HSR, $1392`
- DSP B waits at `$1373`/`$1389`: `brclr HF0, M_HSR, loop`
- DSP C waits at `$1383`: `brclr HF0, M_HSR, $1383`

### 6. Slot Mask Handling

ESAI slot masks determine which time slots carry data in each frame:

| DSP   | TSMA    | TSMB  | RSMA    | RSMB  | Active Slots |
|-------|---------|-------|---------|-------|--------------|
| DSP A | (unset) | (unset) | $FFFF | $F    | 20 RX only   |
| DSP B | $FFFF   | $F    | (unset) | (unset) | 20 TX only |
| DSP C | $FFFF   | $F    | $FFFF   | $F    | 20 TX + 20 RX |

The emulator's ESAI implementation must respect these masks when assembling/disassembling frames.

## ESAI Clock & Timing Analysis

### PLL Configuration (PCTL = `$35000B`)

The DSP56362 PLL multiplies the external crystal oscillator:

| Parameter | Formula | Value |
|-----------|---------|-------|
| pd (predivider) | `((PCTL >> 20) & 0xF) + 1` | 4 |
| df (division factor) | `1 << ((PCTL >> 12) & 3)` | 1 |
| mf (multiplication factor) | `(PCTL & 0xFFF) + 1` | 12 |
| External clock (EXTAL) | `44100 × 768` | **33,868,800 Hz** (~33.9 MHz) |
| DSP core frequency | `EXTAL × mf / (pd × df)` | **101,606,400 Hz** (~101.6 MHz) |

The external clock of 33.87 MHz is measured from real hardware and set in the emulator at `mqdsp.cpp:49`.

### ESAI Bit Clock

The ESAI bit clock runs at the raw EXTAL frequency (**33.87 MHz**), undivided. This is confirmed by the frame rate calculation:

- Frame size: 32 words × 24 bits = **768 bit-clock periods**
- Frame rate: 33,868,800 / 768 = **44,100 Hz** ✓

This means one ESAI word clock period = 33,868,800 / 24 = 1,411,200 words/sec, or equivalently **72 DSP core cycles per word slot** (101.6 MHz / 1,411,200).

### Per-DSP TCCR/RCCR Decode

**DSP A (Clock Master):**

| Register | Value | Key Fields |
|----------|-------|------------|
| TCCR | `$640305` | TCKD=1 (output), TFSD=1 (output), TDC=1 (2 words/frame), TPSR=1, TPM=5 |
| RCCR | `$403E40` | RCKD=0 (input), RFSD=1 (output), RDC=31 (32 words/frame), RPSR=0, RPM=64 |

- DSP A generates the bus-wide bit clock and frame sync (TCKD=1, TFSD=1)
- TX uses 2-word frames (minimal, for clock generation only — no TSMA/TSMB set)
- RX uses 32-word frames to receive audio from expansion DSPs

**DSP B (Sync Master) & DSP C (Sync Slave):**

| Register | Value | Key Fields |
|----------|-------|------------|
| TCCR | `$3E40` | TCKD=0 (input/slave), TDC=31 (32 words/frame), TPSR=0, TPM=64 |
| RCCR | `$403E40` | RFSD=1 (output), RDC=31 (32 words/frame) |

- Both are clock slaves (TCKD=0): receive bit clock from DSP A
- 32-word frames with 20 active slots (TSMA=$FFFF + TSMB=$F)

### Data Rate Summary

| Metric | Value |
|--------|-------|
| ESAI bit clock | **33.87 MHz** |
| Word rate | **1,411,200 words/sec** |
| Frame rate (sample rate) | **44,100 Hz** |
| Words per frame | 32 (TDC/RDC = 31) |
| Active slots per frame | 20 (TSMA=$FFFF + TSMB=$F) |
| Effective data throughput | **21.17 Mbit/s** (20 × 24 bits × 44,100 Hz) |
| Audio channels | 20 channels of 24-bit @ 44.1 kHz |
| DSP core cycles per frame | 2,304 (= 101.6 MHz / 44,100 Hz) |
| DSP core cycles per word slot | 72 (= 2,304 / 32) |

### Why 20 Slots?

The 32-slot ESAI frame has 20 active slots (slots 0–19 enabled). The remaining 12 slots are inactive padding. The 20 active slots carry the inter-DSP audio bus, which needs to transport each expansion DSP's contribution to all internal audio buses:

- Main stereo output (L/R): 2 channels
- Sub/individual outputs: 2+ channels
- Effects sends and returns: multiple channels
- Internal mixing buses: additional channels

With 2 expansion DSPs each sending ~10 channels of audio data to the main DSP, 20 slots provides exactly the bandwidth needed. This is dramatically more capable than the XT's ESSI1 link which only carries 2 channels (stereo) per DSP — the microQ's architecture allows per-output-bus mixing across all 3 DSPs rather than summing a pre-mixed stereo signal.

### Emulator Clock Implications

In the emulator, `EsxiClock::exec()` fires one ESAI word slot every `cyclesPerSample` core cycles (= 1,152 for single-DSP stereo with TDC=1). For the 32-word multi-DSP frame:

- The emulator's base tick rate needs to account for 32 words per frame
- Each `execTX()`/`execRX()` call processes one slot
- With divider=0 and cyclesPerSample=1152, a 32-word frame takes 32 × 1152 = 36,864 cycles = ~2,756 Hz frame rate
- This is 16× too slow; the emulator will need per-DSP divider adjustment or a modified base clock for expansion DSPs to achieve 44.1kHz frame rate with 32-word frames
- The correct relationship: one word slot = 72 core cycles, so `cyclesPerSample` should effectively be 72 for 32-word-frame DSPs (or divider should compensate)

## Key Constants

| Constant     | Value      | Purpose                                    |
|-------------|------------|---------------------------------------------|
| Magic Packet | `$654300`  | ESAI sync acknowledgment value              |
| Boot Trigger | `$BE00`    | Command to start firmware (→ jmp $000100)   |
| Firmware Entry | `$000100` | Correct entry point (includes ESAI init)   |
| Reset Vector | `$000000` → `$001306` | Shortcut entry (skips ESAI init) |
| ESAI Init    | `$00131A`  | Start of patched ESAI/DMA setup code       |
| Sync Loop A  | `$001375`  | DSP A sync polling loop                    |
| Sync Loop C  | `$00136C`  | DSP C sync polling loop                    |
| DMA Buffer   | X:$0EC0-$113F | 640-word sync/audio buffer              |
| Clock Detect | PDRC bit 4 | GPIO pin for ESAI clock presence           |

## Comparison with XT Voice Expansion

| Aspect           | microQ (ESAI)                | XT (ESSI1)                    |
|------------------|------------------------------|-------------------------------|
| Serial Interface | ESAI (complex, network mode) | ESSI (simpler, normal mode)   |
| Topology         | Bus (broadcast)              | Ring (DSP0→DSP1→DSP2→DSP0)   |
| Clock Master     | DSP A                        | External / shared             |
| Sync Mechanism   | DMA → ESAI TX broadcast      | Direct register writes        |
| Magic Value      | `$654300` via ESAI           | `$654300` via ESSI1           |
| Slot Masks       | 20 active / 32 total         | 8 slots                       |
| Audio Channels   | 20 channels (24-bit/44.1kHz) | 2 channels (stereo)           |
| Bit Clock        | 33.87 MHz (= EXTAL)         | Derived from core clock       |
| Data Throughput  | 21.17 Mbit/s                 | ~2.1 Mbit/s                   |
| Sync Initiator   | DSP B (expansion 1)          | Host-driven via boot pump     |
| DMA Usage        | Heavy (sync + audio)         | Moderate (audio only)         |

## Files Reference

| File | Purpose |
|------|---------|
| `source/mqLib/mqdsp.cpp` | DSP boot, HDI08 transfer, diagnostic logging |
| `source/mqLib/mqdsp.h` | DSP class with boot counters |
| `source/mqLib/mqhardware.cpp` | Voice expansion init, ESAI routing |
| `source/mqLib/mqmc.cpp` | 68k microcontroller, HDI08 peripherals |
| `source/mqLib/mqmc.h` | HDI08 address definitions |
| `source/dsp56300/source/dsp56kEmu/esai.cpp` | ESAI emulation (includes OOB fix) |
| `source/dsp56300/source/dsp56kEmu/esai.h` | ESAI register definitions |
