// H8/300H external bus: a flat 16 MiB big-endian space.  The board maps ROM
// and RAM into the flat memory and its peripherals as devices; the core maps
// the on-chip RAM and register field itself (machine.hpp).
#pragma once
#include "common/device.hpp"
#include "common/flatbus.hpp"
#include "cpu/h8300h/types.hpp"

namespace h8300h {

using Bus = emu::FlatBus<24, false>;

}  // namespace h8300h
