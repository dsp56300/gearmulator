// Isolated PIO linkage for the complete fixed 101 graph.  The desktop CMake
// target remains the reference build; this translation unit deliberately
// keeps the board image independent of the production emulator.
#include "../../core/nmm101_kernels.cpp"
#include "../../core/nmm101_filter_control.cpp"
#include "../../core/nmm101_envelope.cpp"
#include "../../core/nmm101_oscillator.cpp"
#include "../../core/nmm101_oscillator_setup.cpp"
#include "../../core/nmm101_graph.cpp"
