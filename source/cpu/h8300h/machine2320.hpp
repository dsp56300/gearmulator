// Machine2320: bus + H8S CPU + scheduler + on-chip peripherals of one
// H8S/2320, the same shape as the H8/3002 Machine (machine.hpp).
//
// Address space (advanced mode): the external areas 0-7 of 2 MB each, timed by
// the bus controller; the 4 KB on-chip RAM at FFEC00-FFFBFF; the register field
// at FFFE00-FFFFFF.  The board maps its memories and devices into the areas.
#pragma once
#include <algorithm>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/h8300h/bus.hpp"
#include "cpu/h8300h/cpu.hpp"
#include "cpu/h8300h/machine.hpp"
#include "cpu/h8300h/periph2320.hpp"

namespace h8300h {

class Machine2320 final {
 public:
  using Periph = Peripherals2320;

  Machine2320()
      : cpu_(bus_, true), io_(Periph::kRegBase, Periph::kRegSize), periph_(sched_, cpu_, bus_, io_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    bus_.map_ram(Periph::kRamBase, Periph::kRamSize, Periph::kClsRam);
    bus_.map_device(Periph::kRegBase, Periph::kRegSize, &io_, Periph::kClsRegisters);
    bus_.set_noexec(Periph::kRegBase, Periph::kRegSize);
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Periph& periph() { return periph_; }
  const Periph& periph() const { return periph_; }

  // External memory and devices, timed by the bus-controller area they sit in.
  void map_ram(u32 base, u32 size) { Machine::map_areas(base, size, [this](u32 b, u32 n, u8 cls) { bus_.map_ram(b, n, cls); }); }
  void map_rom(u32 base, u32 size) { Machine::map_areas(base, size, [this](u32 b, u32 n, u8 cls) { bus_.map_rom(b, n, cls); }); }
  void map_device(u32 base, u32 size, emu::Device* dev) {
    Machine::map_areas(base, size, [this, dev](u32 b, u32 n, u8 cls) { bus_.map_device(b, n, dev, cls); });
  }
  void map_rom_device(u32 base, u32 size, emu::Device* dev) {
    Machine::map_areas(base, size, [this, dev](u32 b, u32 n, u8 cls) { bus_.map_rom_device(b, n, dev, cls); });
  }

  // Current time in states.
  u64 now() const { return cpu_.total_states(); }

  // External reset (RES pin): every module to its reset image, PC from the vector.
  void reset() {
    periph_.reset();
    cpu_.reset();
  }

  // Advance by at least `states`, honouring every scheduled event.  Returns the states actually elapsed.
  u64 run(u64 states) {
    const u64 start = now();
    const u64 end = start + states;
    while (now() < end) {
      sched_.run_due(now());
      cpu_.poll();
      const u64 next = std::min(sched_.next_time(), end);
      cpu_.run(next > now() ? next - now() : 1);
    }
    sched_.run_due(now());
    cpu_.poll();
    return now() - start;
  }
  // One instruction (or one idle state while sleeping), events honoured.
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
  emu::IoMux io_;
  Periph periph_;
};

}  // namespace h8300h
