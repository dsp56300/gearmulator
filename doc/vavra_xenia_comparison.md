# Vavra vs Xenia Audio Processing Comparison

Vavra (microQ) had persistent audio dropouts at low latency settings while Xenia (Waldorf XT) ran flawlessly even with voice expansion. This document captures the architectural differences found during investigation.

## Root Cause: HDI08 RX Rate Limiting

The primary cause was `hdi08().setRXRateLimit(100)` in Vavra vs `0` in Xenia.

| | Vavra (mqLib) | Xenia (xtLib) |
|---|---|---|
| File | `mqdsp.cpp:68` | `xtDSP.cpp:75` |
| Rate limit | 100 cycles | 0 (disabled) |

The 100-cycle minimum gap between HRDF interrupts forced the UC thread to yield repeatedly during HDI08 transfers, creating jitter in the audio processing loop. This was originally modeled after real hardware timing (MC68K@16MHz writing TXH/TXM/TXL + polling TXDE = 20-40 MC68K cycles = 125-250 DSP@100MHz cycles), but the emulated UC is fast enough that the artificial throttle causes stalls instead of smooth pacing.

**Fix:** Set rate limit to 0. Confirmed working in both single-DSP and voice expansion modes.

## Other Notable Differences

### Audio Priming

| | Vavra | Xenia |
|---|---|---|
| Initial empty audio | `writeEmptyAudioIn(8)` | `writeEmptyAudioIn(64)` |
| VE channel priming | 4 frames | 64 frames per ESSI |

Xenia primes 8x more generously, reducing risk of buffer starvation at startup.

### ESAI Clock Configuration

| | Vavra | Xenia |
|---|---|---|
| Normal mode | 44,100 Hz | 320,000 Hz (40k x 8) |
| VE mode | 704,400 Hz (44.1k x 16) | 320,000 Hz (same) |
| TX/RX slots | Asymmetric (2/32 vs 32/2) | Symmetric |

Vavra VE uses a higher clock rate with asymmetric ESAI slot allocation, creating tighter synchronization windows.

### Voice Expansion Boot Synchronization

| | Vavra | Xenia |
|---|---|---|
| Boot mutex | `m_esaiBootMutex` (std::lock_guard) | None (lock-free) |
| DSP output draining | No explicit drain | Drains non-main DSP outputs at >512 frames |

Vavra's boot mutex can block the entire system during VE initialization. Xenia drains expansion DSP output buffers explicitly, preventing overflow.

### HDI08 Callback Complexity

Vavra has a more complex boot/runtime callback switching mechanism protected by `m_hdiCallbackMutex`, with special VE-specific yields on magic ESAI packets. Xenia uses a simpler callback swap with no boot mutex.

### MIDI Processing Order

| | Vavra | Xenia |
|---|---|---|
| MIDI vs audio flag | MIDI processed AFTER `m_processAudio = true` | MIDI processed BEFORE |

Minor difference but adds to contention in Vavra's audio path.

## Summary

The RX rate limit was the dominant factor. The other differences (priming, clock config, boot synchronization) are secondary but may become relevant under extreme load or during voice expansion initialization.
