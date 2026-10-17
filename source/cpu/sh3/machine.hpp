// Machine: bus + CPU + scheduler + on-chip peripherals for one SH7709A.
//
// The board maps its memory and devices on the physical space (mirrored
// into P0-P3 by the bus), connects the pins of the peripheral block and
// calls run(); events are honoured to the state.
#pragma once
#include <algorithm>

#include "common/sched.hpp"
#include "cpu/sh3/bus.hpp"
#include "cpu/sh3/cpu.hpp"
#include "cpu/sh3/periph.hpp"

namespace sh3 {

class Machine final {
 public:
  // cpu_clock_hz: the CPU clock; frqcr_reset: the FRQCR value selected by the
  // mode pins (CKOEN set, I-phi = PLL, P-phi = PLL / 4 by default).
  explicit Machine(u64 cpu_clock_hz = 60'000'000, u16 frqcr_reset = 0x0102)
      : cpu_(bus_), periph_(sched_, cpu_, bus_, cpu_clock_hz, frqcr_reset) {
    periph_.map(bus_);
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  Peripherals& periph() { return periph_; }
  const Peripherals& periph() const { return periph_; }
  u64 cpu_clock_hz() const { return periph_.cpu_clock_hz(); }

  // Physical-space mappings (aliased in P0-P3).
  void map_ram(u32 phys, u32 size, u8 cls = Bus::kClsFast) { bus_.map_physical_ram(phys, size, cls); }
  void map_rom(u32 phys, u32 size, u8 cls = Bus::kClsFast) { bus_.map_physical_rom(phys, size, cls); }
  void map_device(u32 phys, u32 size, emu::Device* dev, u8 cls = Bus::kClsFast) { bus_.map_physical_device(phys, size, dev, cls); }
  void map_rom_device(u32 phys, u32 size, emu::Device* dev, u8 cls = Bus::kClsFast) { bus_.map_physical_rom_device(phys, size, dev, cls); }
  void mirror(u32 phys, u32 size, u32 target) { bus_.mirror_physical(phys, size, target); }

  u64 now() const { return cpu_.total_states(); }

  // Power-on reset.
  void reset() {
    periph_.reset();
    cpu_.reset();
  }

  // Advance the machine by at least `states`, honouring every scheduled
  // event.  Returns the number of states actually elapsed.
  u64 run(u64 states) {
    const u64 start = now();
    const u64 end = start + states;
    while (now() < end) {
      sched_.run_due(now());
      cpu_.poll();
      const u64 next = std::min(sched_.next_time(), end);
      const u64 want = next > now() ? next - now() : 1;
      cpu_.run(want);
    }
    sched_.run_due(now());
    cpu_.poll();
    return now() - start;
  }
  // One instruction, events honoured.
  u64 step() {
    const u64 start = now();
    sched_.run_due(now());
    cpu_.poll();
    cpu_.step();
    sched_.run_due(now());
    return now() - start;
  }

 private:
  Bus bus_;
  Cpu cpu_;
  emu::Scheduler sched_;
  Peripherals periph_;
};

}  // namespace sh3
