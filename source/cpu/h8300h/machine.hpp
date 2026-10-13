// Machine: bus + CPU + scheduler + on-chip peripherals for one H8/3002.
//
// The board maps its ROM, RAM and external devices on the bus and claims the
// register-field bytes of the modules the chip leaves to it (the A/D
// converter, the ports) with map_register(); run() advances time in slices
// bounded by the next scheduled event.
#pragma once
#include <algorithm>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/h8300h/bus.hpp"
#include "cpu/h8300h/cpu.hpp"
#include "cpu/h8300h/periph.hpp"

namespace h8300h {

class Machine final : public ResetSink {
 public:
  // Register-field line bytes below the first register: FFFF00-FFFF0F is the
  // tail of the on-chip RAM, FFFF10-FFFF1B is external space (the board's
  // catch-all, if any); bytes above that nobody claims read all ones.
  struct RegFieldFallback final : emu::Device {
    explicit RegFieldFallback(Bus& bus) : bus(bus) {}
    u8 read8(u32 a) override {
      if (a <= kRamEnd) return bus.mem()[a & kAddrMask];
      if (a < 0xFFFF1C && bus.unmapped_device()) return bus.unmapped_device()->read8(a);
      return 0xFF;
    }
    void write8(u32 a, u8 v) override {
      if (a <= kRamEnd) bus.mem()[a & kAddrMask] = v;
      else if (a < 0xFFFF1C && bus.unmapped_device()) bus.unmapped_device()->write8(a, v);
    }
    Bus& bus;
  };

  explicit Machine(int operating_mode = 4)
      : cpu_(bus_), io_(kRegFieldBase, 0x100), periph_(sched_, cpu_, bus_, io_), fallback_(bus_) {
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    periph_.set_operating_mode(operating_mode);
    periph_.set_reset_sink(this);
    // On-chip RAM (FFFD10-FFFF0F; the lines below FFFF00 are plain RAM, the
    // last 16 bytes share the register field's line and go through the mux).
    bus_.map_ram(kRamBase & ~(Bus::kLineSize - 1), kRegFieldBase - (kRamBase & ~(Bus::kLineSize - 1)), kClsIntRam);
    bus_.map_device(kRegFieldBase, 0x100, &io_, kClsRegs);
    io_.set_fallback(&fallback_);
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Peripherals& periph() { return periph_; }
  const Peripherals& periph() const { return periph_; }

  // A board device on register-field bytes (ports, A/D converter, ...).
  void map_register(u32 addr, u32 len, emu::Device* dev) { io_.assign(addr, len, dev); }

  // External memory and devices, timed by the bus-controller area they sit
  // in (2 MB each in advanced mode); a range crossing an area boundary is
  // split so every line carries its own area's access class.
  void map_ram(u32 base, u32 size) { map_areas(base, size, [this](u32 b, u32 n, u8 cls) { bus_.map_ram(b, n, cls); }); }
  void map_rom(u32 base, u32 size) { map_areas(base, size, [this](u32 b, u32 n, u8 cls) { bus_.map_rom(b, n, cls); }); }
  void map_device(u32 base, u32 size, emu::Device* dev) {
    map_areas(base, size, [this, dev](u32 b, u32 n, u8 cls) { bus_.map_device(b, n, dev, cls); });
  }
  void map_rom_device(u32 base, u32 size, emu::Device* dev) {
    map_areas(base, size, [this, dev](u32 b, u32 n, u8 cls) { bus_.map_rom_device(b, n, dev, cls); });
  }
  static u8 area_class(u32 addr) { return u8(kClsArea0 + ((addr & kAddrMask) >> 21)); }

  // Current time in states.
  u64 now() const { return cpu_.total_states(); }

  template <class F>
  static void map_areas(u32 base, u32 size, F map) {
    while (size) {
      const u32 area_end = ((base >> 21) + 1) << 21;
      const u32 n = std::min(size, area_end - base);
      map(base, n, area_class(base));
      base += n;
      size -= n;
    }
  }

  // External reset (RES pin): every module to its reset image, PC from the vector.
  void reset() {
    periph_.reset();
    cpu_.reset();
    ++resets_;
  }
  // The watchdog's reset: taken at the next instruction boundary.
  void watchdog_reset() override { reset_pending_ = true; cpu_.request_reset(); }
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
      if (reset_pending_) { reset_pending_ = false; reset(); }
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
    if (reset_pending_) { reset_pending_ = false; reset(); }
    sched_.run_due(now());
    return now() - start;
  }

 private:
  Bus bus_;
  Cpu cpu_;
  emu::Scheduler sched_;
  emu::IoMux io_;
  Peripherals periph_;
  RegFieldFallback fallback_;
  bool reset_pending_ = false;
  u64 resets_ = 0;
};

}  // namespace h8300h
