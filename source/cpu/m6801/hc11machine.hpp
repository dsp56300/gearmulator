// Hc11Machine: bus + CPU + scheduler + on-chip peripherals for one
// MC68HC11A0 in the expanded multiplexed mode.
//
// The chip overlays two places: its 256 bytes of RAM at $0000-$00FF, which
// win over anything external there, and the register block at $1000-$103F
// (an IoMux over the bus line $1000-$107F; the bytes the chip does not
// decode, the port B / C registers and $1040 up, fall through to the board's
// mapping of that line). The board maps its memory with map_ram / map_rom /
// map_device as its own address decoder sees it. run() advances time in
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
#include "cpu/m6801/hc11periph.hpp"

namespace m6801 {

class Hc11Machine final {
 public:
  explicit Hc11Machine(ChipModel model = ChipModel::MC68HC11A0, int mode = 1)
      : model_(model), cpu_(bus_, model), io_(kRegLine, Bus::kLineSize), periph_(sched_, cpu_, io_), ext_(*this) {
    assert(family_of(model) == Family::HC11 && mode_supported(model, mode));
    (void)mode;
    sched_.set_earlier_hook([](void* c, u64 when) { static_cast<Cpu*>(c)->cut_slice(when); }, &cpu_);
    bus_.map_ram(hc11::kRamBase, hc11::kRamSize);
    ext_line_ = bus_.save_line(kRegLine);
    bus_.map_device(kRegLine, Bus::kLineSize, &io_);
    io_.set_fallback(&ext_);
  }

  Bus& bus() { return bus_; }
  const Bus& bus() const { return bus_; }
  Cpu& cpu() { return cpu_; }
  const Cpu& cpu() const { return cpu_; }
  emu::Scheduler& sched() { return sched_; }
  emu::IoMux& io() { return io_; }
  Hc11Peripherals& periph() { return periph_; }
  const Hc11Peripherals& periph() const { return periph_; }
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

  // Current time in E cycles.
  u64 now() const { return cpu_.total_states(); }

  // External reset (RESET pin): every module to its reset image, PC from the vector.
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
  // One instruction (or one idle state while waiting / stopped), events honoured.
  u64 step() {
    const u64 start = now();
    sched_.run_due(now());
    cpu_.poll();
    cpu_.step();
    sched_.run_due(now());
    return now() - start;
  }

 private:
  static constexpr u32 kRegLine = hc11::kRegBase;

  // The bytes of the register line the chip does not decode: the board's mapping of it.
  struct ExternalRegLine final : emu::Device {
    explicit ExternalRegLine(Hc11Machine& m) : m(m) {}
    u8 read8(u32 a) override {
      const Bus::LineState& s = m.ext_line_;
      switch (s.kind) {
        case Bus::Line::Ram: case Bus::Line::Rom: case Bus::Line::RomDev: return m.bus_.mem()[a];
        case Bus::Line::Dev: return s.dev->read8(a);
        default: return m.bus_.unmapped_device() ? m.bus_.unmapped_device()->read8(a) : u8(0xFF);
      }
    }
    void write8(u32 a, u8 v) override {
      const Bus::LineState& s = m.ext_line_;
      switch (s.kind) {
        case Bus::Line::Ram: m.bus_.mem()[a] = v; m.cpu_.code_written(a); break;
        case Bus::Line::Dev: case Bus::Line::RomDev: s.dev->write8(a, v); break;
        case Bus::Line::Unmapped: if (m.bus_.unmapped_device()) m.bus_.unmapped_device()->write8(a, v); break;
        default: break;
      }
    }
    Hc11Machine& m;
  };

  // The internal RAM keeps its lines; the register line records the board's
  // mapping under the overlay; the rest is mapped directly.
  template <class F>
  void map(u32 base, u32 size, F map_lines) {
    const u32 end = base + size;
    u32 a = std::max(base, u32(hc11::kRamSize));
    while (a < end) {
      if (a >= kRegLine && a < kRegLine + Bus::kLineSize) {
        const Bus::LineState keep = bus_.save_line(kRegLine);
        std::array<u8, Bus::kLineSize> bytes;
        std::memcpy(bytes.data(), bus_.mem() + kRegLine, bytes.size());
        map_lines(kRegLine, Bus::kLineSize);
        ext_line_ = bus_.save_line(kRegLine);
        bus_.restore_line(kRegLine, keep);
        std::memcpy(bus_.mem() + kRegLine, bytes.data(), bytes.size());
        a = kRegLine + Bus::kLineSize;
        continue;
      }
      const u32 stop = a < kRegLine ? std::min(end, kRegLine) : end;
      map_lines(a, stop - a);
      a = stop;
    }
  }

  ChipModel model_;
  Bus bus_;
  Cpu cpu_;
  emu::Scheduler sched_;
  emu::IoMux io_;
  Hc11Peripherals periph_;
  ExternalRegLine ext_;
  Bus::LineState ext_line_{};
  u64 resets_ = 0;
};

}  // namespace m6801
