// The isolated PIO environment owns this translation unit so the portable
// native kernel is linked into the Teensy image without changing the desktop
// build or production emulator targets.
#include "../../core/nmm101_kernels.cpp"
