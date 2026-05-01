# Vavra Performance Regression: 2.2.2 → 2.2.3

The voice expansion merge introduced several changes that affect Vavra's audio processing performance even in single-DSP (non-VE) mode.

## 1. HDI08 RX Rate Limit: 0 → 100 [FIXED]

`mqdsp.cpp:68` — `hdi08().setRXRateLimit(100)`

In 2.2.2 the rate limit was 0. The VE merge set it to 100 cycles to model real hardware timing (MC68K@16MHz writing TXH/TXM/TXL). This forced the UC thread to yield repeatedly during HDI08 transfers, creating jitter and dropouts at low latency.

**Fix:** Reverted to 0 in commit `2fc51971`.

## 2. Mutex on Every HDI08 Word Transfer [OPEN]

In 2.2.2, the HDI08 write callback was **swapped** at the boot→runtime boundary (lock-free):

```cpp
// Boot mode:
m_hdiUC.setWriteTxCallback([&](const uint32_t _word) {
    if(m_boot.hdiWriteTX(_word))
        onDspBootFinished();
});
// After boot, replaced with:
m_hdiUC.setWriteTxCallback([&](const uint32_t _word) {
    hdiTransferUCtoDSP(_word);
});
```

In 2.2.3, a single callback uses `m_hdiCallbackMutex` to guard boot/runtime mode:

```cpp
m_hdiUC.setWriteTxCallback([this](const uint32_t _word) {
    std::lock_guard lock(m_hdiCallbackMutex);
    if (m_inBootMode)
        ...
    else
        hdiTransferUCtoDSP(_word);
});
```

The mutex is taken on every single HDI08 word transfer — thousands per audio block. Even uncontended, `std::mutex` has non-trivial overhead (atomic operations, potential OS calls). In non-VE mode the mutex is never contended but the cost is still paid.

**Potential fix:** Use `std::atomic<bool>` for `m_inBootMode` and eliminate the mutex, or restore the callback-swap approach for non-VE mode.

## 3. TXDE Back-Pressure Wait in `hdiTransferUCtoDSP` [OPEN]

New in 2.2.3 — before every `hdi08().writeRX()`, the code waits for the DSP to consume previous RX data:

```cpp
if (hdi08().hasRXData() && hdi08().rxInterruptEnabled())
{
    m_hardware.ucYieldLoop([&]() {
        return hdi08().hasRXData() && hdi08().rxInterruptEnabled();
    });
}
```

In 2.2.2 this wait did not exist; writes went through directly. The wait was added for VE correctness (real hardware has natural MC68K bus timing that paces transfers), but it affects non-VE mode identically.

## 4. BatchStart/BatchComplete Synchronization in `hdiSendIrqToDSP` [OPEN]

New in 2.2.3 — waits for DSP command processing to complete before starting a new batch:

```cpp
if (cmd == HostCommand::BatchStart && m_commandProcessingActive)
{
    m_hardware.ucYieldLoop([&]() {
        return (m_memory.get(dsp56k::MemArea_Y, 6) & 1) == 0;
    });
    m_commandProcessingActive = false;
}
```

On real hardware the MC68K is slow enough that processing always finishes between batches. The emulated UC is faster, so this explicit synchronization was added. However, it adds ucYieldLoop calls that weren't there before.

## 5. `if constexpr` → Runtime `if` in `processAudio` [OPEN]

```cpp
// 2.2.2:
if constexpr (g_useVoiceExpansion) { ... }
// 2.2.3:
if (m_useVoiceExpansion) { ... }
```

The VE code path now compiles into the binary even in non-VE mode. This increases function size and I-cache pressure. The compiler cannot eliminate the dead VE branch since `m_useVoiceExpansion` is a runtime member variable.

## Impact Assessment

| Change | Severity | Affects non-VE | Fixed |
|--------|----------|----------------|-------|
| RX rate limit 0→100 | High | Yes | Yes |
| Mutex per HDI08 word | Medium | Yes | No |
| TXDE back-pressure wait | Medium | Yes | No |
| Batch sync wait | Low-Medium | Yes | No |
| constexpr → runtime if | Low | Yes | No |
