# Native 101 DSP core

This directory is a freestanding, fixed point implementation of the module
boundaries used by the generated `101.pch` graph.  It is intentionally free of
JUCE, STL allocation, and floating point so the same sources can be compiled
by PlatformIO for Teensy 4.1.

`dsp56300_fixed.h` uses the DSP56303 mathematical representation directly:

* X/Y words are masked 24-bit signed Q1.23 values.
* MPY/MAC products are `signed24 * signed24 << 1`.
* An accumulator load to A/B places the word in A1/B1 (`word << 24`).
* A1/B1 transfers use `accumulator >> 24`.
* ALU results wrap to 56 bits and arithmetic shifts round toward negative
  infinity, matching the 56300 rather than C++'s division rule.

`runFilterF92` implements the raw 092 sample template; `runCompiledFilterF92`
implements its actual 101 compiler boundary. The selected oscillator, filter
control, envelope and output each have DSP differential tests. `Graph101`
connects them and caches fixed coefficient calculations while preserving the
phase, envelope and filter updates. Call `invalidateCoefficients()` after
changing coefficient or table words through the diagnostic memory accessor.

The complete connected graph passes the desktop interpreter/JIT corpus and
its matching Teensy 4.1 hardware oracle. See [validation](../VALIDATION.md)
for the measured scope and limitations.

The fixed arithmetic test is standalone and can be run with any C++17 host
compiler.  It is also suitable for PlatformIO as a native unit-test source;
the implementation has no platform dependencies.
