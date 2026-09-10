#pragma once
#include "nmm101_kernels.h"

namespace nmm::native {
// OS 3.03b ADSR control fragment as specialized in 101: unconnected amplitude
// control input = 0, gate X:0e, fixed gain word 0x200000, output X:13.
// Cursor is at the start of the module's X/Y control allocations. The shared
// firmware compiler may hoist its first pair of loads into the preceding
// module; preloaded=true reproduces that boundary using existing X0/Y0.
Word24 runEnvelope20Control(ModuleCursor& c, bool preloaded = false) noexcept;
}
