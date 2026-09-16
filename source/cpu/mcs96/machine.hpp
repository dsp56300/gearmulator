// Machine: bus + CPU + scheduler + on-chip peripherals for one MCS-96 device
// (an 8x9x or an 80C196KB).  The board maps its ROM, RAM and I/O on the bus
// and drives the pins of the peripheral block; run() advances time in slices
// bounded by the next scheduled event.
#pragma once
#include <algorithm>

#include "common/sched.hpp"
#include "cpu/mcs96/bus.hpp"
#include "cpu/mcs96/cpu.hpp"
#include "cpu/mcs96/periph.hpp"

namespace mcs96 {

class Machine final : public ResetSink {
 public:
  explicit Machine(Variant variant) : cpu_(bus_, variant), periph_(sched_, cpu_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    periph_.set_reset_sink(this);
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  Peripherals& periph() { return periph_; }
  const Peripherals& periph() const { return periph_; }
  Variant variant() const { return cpu_.variant(); }

  // Current time in states.
  u64 now() const { return cpu_.total_states(); }

  // External reset (RESET pin): CPU and SFRs to their reset image.
  void reset() {
    cpu_.reset();
    ++resets_;
  }
  // The KB's watchdog overflow: same as a reset for everything on chip.
  void watchdog_reset() override { reset(); }
  u64 resets() const { return resets_; }

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

 private:
  Bus bus_;
  Cpu cpu_;
  emu::Scheduler sched_;
  Peripherals periph_;
  u64 resets_ = 0;
};

}  // namespace mcs96
