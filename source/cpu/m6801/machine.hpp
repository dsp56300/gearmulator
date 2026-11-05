// Machine: bus + CPU + scheduler + on-chip peripherals for one MC6803 or
// HD6303R.
//
// The chip overlays the first two bus lines: the register field (the IoMux
// over $0000-$007F, whose bytes the chip does not decode reach the external
// bus) and the internal RAM at $0080-$00FF (while RAME is set and the mode
// has it). The board maps its memory with map_ram / map_rom / map_device as
// its own address decoder sees it; under the overlay that mapping is kept
// as the external view the chip falls through to. run() advances time in
// slices bounded by the next scheduled event.
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/m6801/bus.hpp"
#include "cpu/m6801/cpu.hpp"
#include "cpu/m6801/periph.hpp"

namespace m6801 {

class Machine final {
 public:
  // `mode`: the operating mode strapped on P20-P22 (MC6803: 2 or 3;
  // HD6303R: 1, 2 or 4).
  Machine(ChipModel model, int mode)
      : model_(model),
        cpu_(bus_, model),
        io_(0x0000, kRamBase),
        periph_(sched_, cpu_, io_, model, mode),
        low_(*this) {
    assert(mode_supported(model, mode));
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    cpu_.set_mode(mode);
    ext_[0] = bus_.save_line(0x0000);
    ext_[1] = bus_.save_line(kRamBase);
    bus_.map_device(0x0000, kRamBase, &io_);
    io_.set_fallback(&low_);
    if (has_internal_ram(family_of(model), mode)) {
      bus_.map_ram(kRamBase, kRamSize);
      int_ram_ = bus_.save_line(kRamBase);
      ram_in_ = true;
      periph_.set_ram_enable_hook([this](bool on) { place_internal_ram(on); });
    }
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Peripherals& periph() { return periph_; }
  const Peripherals& periph() const { return periph_; }
  ChipModel model() const { return model_; }

  // External memory and devices (128-byte granularity).
  void map_ram(u32 base, u32 size) { map(base, size, [this](u32 b, u32 n) { bus_.map_ram(b, n); }); }
  void map_rom(u32 base, u32 size) { map(base, size, [this](u32 b, u32 n) { bus_.map_rom(b, n); }); }
  void map_device(u32 base, u32 size, emu::Device* dev) {
    map(base, size, [this, dev](u32 b, u32 n) { bus_.map_device(b, n, dev); });
  }
  void map_rom_device(u32 base, u32 size, emu::Device* dev) {
    map(base, size, [this, dev](u32 b, u32 n) { bus_.map_rom_device(b, n, dev); });
  }
  // A board device on single bytes of $0000-$007F that the chip leaves to
  // the bus (the port 3 / 4 registers in the expanded modes, $0020 up).
  void map_register(u32 addr, u32 len, emu::Device* dev) { io_.assign(addr, len, dev); }

  // Current time in E cycles.
  u64 now() const { return cpu_.total_states(); }

  // External reset (RES pin): every module to its reset image, PC from the vector.
  void reset() {
    periph_.reset();
    cpu_.reset();
    ++resets_;
  }
  u64 resets() const { return resets_; }

  // Advance the machine by at least `states`, honouring every scheduled
  // event. Returns the number of states actually elapsed.
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
  // One instruction (or one idle state while waiting / sleeping), events honoured.
  u64 step() {
    const u64 start = now();
    sched_.run_due(now());
    cpu_.poll();
    cpu_.step();
    sched_.run_due(now());
    return now() - start;
  }

 private:
  // The register-field bytes the chip does not decode: the board's mapping of $0000-$007F.
  struct ExternalLow final : emu::Device {
    explicit ExternalLow(Machine& m) : m(m) {}
    u8 read8(u32 a) override {
      const Bus::LineState& s = m.ext_[0];
      switch (s.kind) {
        case Bus::Line::Ram: case Bus::Line::Rom: case Bus::Line::RomDev: return m.bus_.mem()[a];
        case Bus::Line::Dev: return s.dev->read8(a);
        default: return m.bus_.unmapped_device() ? m.bus_.unmapped_device()->read8(a) : u8(0xFF);
      }
    }
    void write8(u32 a, u8 v) override {
      const Bus::LineState& s = m.ext_[0];
      switch (s.kind) {
        case Bus::Line::Ram: m.bus_.mem()[a] = v; m.cpu_.code_written(a); break;
        case Bus::Line::Dev: case Bus::Line::RomDev: s.dev->write8(a, v); break;
        case Bus::Line::Unmapped: if (m.bus_.unmapped_device()) m.bus_.unmapped_device()->write8(a, v); break;
        default: break;
      }
    }
    Machine& m;
  };

  // The lines under the overlay record the board's mapping; the rest of the
  // range is mapped directly.
  template <class F>
  void map(u32 base, u32 size, F map_lines) {
    const u32 end = base + size;
    for (u32 line = 0; line < 2; ++line) {
      const u32 b = line * Bus::kLineSize;
      if (base > b || end < b + Bus::kLineSize) continue;
      if (line == 1 && !ram_in_) {
        map_lines(b, Bus::kLineSize);  // nothing overlays it now
        ext_[1] = bus_.save_line(b);
        continue;
      }
      // Map it for a moment to capture the line state, then put the overlay back.
      const Bus::LineState keep = bus_.save_line(b);
      std::array<u8, Bus::kLineSize> bytes;
      std::memcpy(bytes.data(), bus_.mem() + b, bytes.size());
      map_lines(b, Bus::kLineSize);
      ext_[line] = bus_.save_line(b);
      const bool ram = bus_.line_kind(b) == Bus::Line::Ram;
      bus_.restore_line(b, keep);
      std::memcpy(bus_.mem() + b, bytes.data(), bytes.size());
      if (ram) {
        if (line == 0) std::memset(bus_.mem(), 0, kRamBase);  // the external RAM image lives under the IoMux
        else stash_.fill(0);
      }
    }
    const u32 lo = std::max(base, u32(2 * Bus::kLineSize));
    if (end > lo) map_lines(lo, end - lo);
  }

  // RAME: the internal RAM in or out of $0080-$00FF. The bytes not in place
  // wait in the stash (internal RAM keeps its data while disabled).
  void place_internal_ram(bool on) {
    if (on == ram_in_) return;
    u8* line = bus_.mem() + kRamBase;
    for (u32 i = 0; i < kRamSize; ++i) std::swap(line[i], stash_[i]);
    if (on) {
      bus_.restore_line(kRamBase, int_ram_);
    } else {
      Bus::LineState s = ext_[1];
      if (s.kind == Bus::Line::Unmapped) s.dev = bus_.unmapped_device();
      bus_.restore_line(kRamBase, s);
    }
    ram_in_ = on;
    cpu_.invalidate_range(kRamBase, kRamSize);
  }

  ChipModel model_;
  Bus bus_;
  Cpu cpu_;
  emu::Scheduler sched_;
  emu::IoMux io_;
  Peripherals periph_;
  ExternalLow low_;
  std::array<Bus::LineState, 2> ext_{};  // the board's mapping of $0000-$007F and $0080-$00FF
  Bus::LineState int_ram_{};
  std::array<u8, kRamSize> stash_{};
  bool ram_in_ = false;
  u64 resets_ = 0;
};

}  // namespace m6801
