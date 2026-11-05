// MC6801 / HD6301 address space: 64 KB, big-endian, on the shared flat bus.
// The machine overlays the register field ($0000-$007F line) and the
// internal RAM ($0080-$00FF line); everything else is the board's.
#pragma once
#include "common/device.hpp"
#include "common/flatbus.hpp"
#include "cpu/m6801/types.hpp"

namespace m6801 {

using Bus = emu::FlatBus<16, false>;

}  // namespace m6801
