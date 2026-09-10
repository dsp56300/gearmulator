#include <Arduino.h>

#ifdef NMM_RECOVERY_LED

// Small recovery image.  If this image blinks, Teensy startup, the selected
// CPU clock, and the LED pin are healthy; the diagnostic image can then be
// bisected without conflating a native-kernel fault with board bring-up.
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalToggleFast(LED_BUILTIN);
    delay(500);
}

#else

#include "filter_f92_board_runner.h"

// Bare-board transport/timer bring-up. Native 101 validation is integrated
// separately; this diagnostic does not claim to synthesize the patch.
namespace {
uint32_t midiEvents = 0;
uint32_t previousReport = 0;
uint32_t cycleCounterOverhead = 0;
nmm::native::teensy::FilterF92BoardResult filterResult{};

void runFilterOracle() {
    filterResult = nmm::native::teensy::runFilterF92Vectors(
        []() { return ARM_DWT_CYCCNT; }, cycleCounterOverhead);
}

void report() {
    Serial.print("{\"target\":\"teensy36\",\"stage\":\"filter-f92-oracle\",\"cpu_hz\":");
    Serial.print(F_CPU);
    Serial.print(",\"cycle_counter_overhead\":");
    Serial.print(cycleCounterOverhead);
    Serial.print(",\"midi_events\":");
    Serial.print(midiEvents);
    Serial.print(",\"vectors\":");
    Serial.print(static_cast<unsigned long>(filterResult.vectors));
    Serial.print(",\"passed\":");
    Serial.print(static_cast<unsigned long>(filterResult.passed));
    Serial.print(",\"failed\":");
    Serial.print(static_cast<unsigned long>(filterResult.failed));
    Serial.print(",\"first_failure\":");
    if(filterResult.firstFailure == static_cast<std::size_t>(-1))
        Serial.print(-1);
    else
        Serial.print(static_cast<unsigned long>(filterResult.firstFailure));
    Serial.print(",\"cycles_total\":");
    Serial.print(static_cast<unsigned long long>(filterResult.cyclesTotal));
    Serial.print(",\"cycles_min\":");
    Serial.print(filterResult.cyclesMin);
    Serial.print(",\"cycles_max\":");
    Serial.print(filterResult.cyclesMax);
    Serial.println(",\"native_101_verified\":false}");
}
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.begin(115200);
    ARM_DEMCR |= ARM_DEMCR_TRCENA;
    ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
    const uint32_t begin = ARM_DWT_CYCCNT;
    const uint32_t end = ARM_DWT_CYCCNT;
    cycleCounterOverhead = end - begin;
    runFilterOracle();
}

void loop() {
    // Bound servicing even if a host continually sends MIDI.
    for (unsigned i = 0; i < 64 && usbMIDI.read(); ++i) ++midiEvents;
    if (Serial.available()) {
        const int command = Serial.read();
        if (command == '?' || command == 'r') report();
        if (command == 'f') {
            runFilterOracle();
            report();
        }
    }
    const uint32_t now = millis();
    if (now - previousReport >= 1000) {
        previousReport = now;
        digitalToggleFast(LED_BUILTIN);
        if (Serial) report();
    }
}

#endif
