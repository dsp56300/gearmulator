// SH7014 watchdog timer (manual section 11).
#include "cpu/sh2/machine.hpp"
#include "cpu/sh2/wdt.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kTcsr = 0xFFFF8610u, kTcnt = 0xFFFF8611u, kRstcsrW = 0xFFFF8612u, kRstcsrR = 0xFFFF8613u;
constexpr u32 kIprh = 0xFFFF8356u;

struct Sink final : WdtResetSink {
  int resets = 0;
  u64 at = 0;
  Machine* m = nullptr;
  Wdt* wdt = nullptr;
  void watchdog_reset() override {
    ++resets;
    at = m->now();
    wdt->reset(true);  // what the Machine does: everything resets, RSTCSR survives
  }
};

struct System {
  Machine m;
  Wdt wdt;
  Sink sink;
  static constexpr u32 kRam = 0xFFFFF000u;
  System() : m(ChipModel::SH7014, 1), wdt(m.sched(), m.cpu(), m.intc()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    wdt.map(m.io());
    sink.m = &m;
    sink.wdt = &wdt;
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};  // MOV #v,R0 ; MOV.L R0,@R1 ; RTE ; NOP
      u32 a = h;
      for (u16 w : code) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    }
    const u16 loop[] = {0x0009, 0xAFFD, 0x0009};  // NOP ; BRA -6 ; slot NOP
    u32 a = kRam;
    for (u16 w : loop) { Bus::put_be16(m.bus().ptr(a), w); a += 2; }
    m.bus().write32(0, kRam);
    m.bus().write32(4, 0xFFFFFBF0u);
    m.cpu().invalidate_all();
    m.reset();
    wdt.reset();
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
  }
  Bus& bus() { return m.bus(); }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

void test_register_protocol() {
  System s;
  Bus& b = s.bus();
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  // Byte writes are ignored.
  b.write8(kTcsr, 0xFF);
  b.write8(kTcnt, 0x55);
  b.write8(kRstcsrW, 0x40);
  b.write8(kRstcsrR, 0x40);
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  // Longword writes too.
  b.write32(kTcsr, 0x5A55A5C7u);
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  // Word writes need the password.
  b.write16(kTcsr, 0x1207);  // wrong password
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  b.write16(kTcsr, 0x5A55);  // TCNT
  CHECK_EQ(b.read8(kTcnt), 0x55);
  CHECK_EQ(b.read8(kTcsr), 0x18);
  b.write16(kTcsr, 0xA5E7);  // TCSR: OVF cannot be set; WT/IT, TME, CKS take
  CHECK_EQ(b.read8(kTcsr), 0x7F);
  CHECK_EQ(b.read8(kTcnt), 0x55);  // TME 0 -> 1 keeps the preset count
  b.write16(kTcsr, 0xA500);  // TME = 0 clears the counter
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  // RSTCSR: RSTE via H'5A, only bit 6 is stored.
  b.write16(kRstcsrW, 0x5AFF);
  CHECK_EQ(b.read8(kRstcsrR), 0x5F);
  b.write16(kRstcsrW, 0xA500);  // WOVF clear does not touch RSTE
  CHECK_EQ(b.read8(kRstcsrR), 0x5F);
  b.write16(kRstcsrW, 0x5A00);
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  b.write16(kRstcsrW, 0x1240);  // wrong password
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  // Through the CPU: MOV.W R0,@R2 must land as one word.
  Bus::put_be16(b.ptr(0x2000), 0x2201);  // MOV.W R0,@R2
  Bus::put_be16(b.ptr(0x2002), 0x0009);
  s.m.cpu().invalidate_all();
  s.m.cpu().regs().pc = 0x2000;
  s.m.cpu().regs().r[0] = 0x5A12;
  s.m.cpu().regs().r[2] = kTcsr;
  s.m.cpu().step();
  CHECK_EQ(b.read8(kTcnt), 0x12);
}

// Count rates at phi/2 and phi/256 read back exactly.
void test_count_rates() {
  System s;
  Bus& b = s.bus();
  u64 t0 = s.m.now();
  b.write16(kTcsr, 0xA520);  // TME, interval, phi/2
  s.m.run(300);
  u64 now = s.m.now();
  CHECK_EQ(b.read8(kTcnt), u8((now >> 1) - (t0 >> 1)));
  CHECK(b.read8(kTcnt) >= 150);
  s.m.run(100);
  now = s.m.now();
  CHECK_EQ(b.read8(kTcnt), u8((now >> 1) - (t0 >> 1)));
  b.write16(kTcsr, 0xA500);
  CHECK_EQ(b.read8(kTcnt), 0);
  t0 = s.m.now();
  b.write16(kTcsr, 0xA523);  // TME, phi/256
  s.m.run(256 * 37 + 100);
  now = s.m.now();
  CHECK_EQ(b.read8(kTcnt), u8((now >> 8) - (t0 >> 8)));
  CHECK(b.read8(kTcnt) >= 37);
  CHECK_EQ(b.read8(kTcsr), 0x3B);  // no OVF yet
  // Writing TCNT reloads the count in place (the watchdog "kick").
  b.write16(kTcsr, 0x5A10);
  const u64 t1 = s.m.now();
  s.m.run(256 * 5);
  now = s.m.now();
  CHECK_EQ(b.read8(kTcnt), u8(0x10 + (now >> 8) - (t1 >> 8)));
}

// Interval timer mode: overflow at the exact state sets OVF and requests ITI (vector 152).
void test_interval_mode() {
  System s;
  Bus& b = s.bus();
  b.write16(kIprh, 0x6000);  // WDT level 6
  s.m.cpu().regs().sr |= Cpu::kIMask;
  b.write16(kTcsr, 0x5AF0);  // TCNT = H'F0: 16 counts to overflow
  const u64 t0 = s.m.now();
  b.write16(kTcsr, 0xA520);  // TME, interval, phi/2
  const u64 ovf = ((t0 >> 1) + 16) << 1;
  u64 last_clear = 0, first_set = 0;
  for (int i = 0; i < 100 && !first_set; ++i) {
    s.m.run(1);
    const u64 now = s.m.now();
    if (b.read8(kTcsr) & 0x80) first_set = now;
    else last_clear = now;
  }
  CHECK(first_set != 0);
  CHECK(last_clear < ovf);
  CHECK(first_set >= ovf);
  CHECK(first_set - ovf <= 2);
  CHECK_EQ(b.read8(kTcsr), 0xB8);
  CHECK_EQ(b.read8(kTcnt), u8((s.m.now() >> 1) - (t0 >> 1) - 16));  // wrapped, still counting
  CHECK(s.m.intc().request(IrqSrc::Iti));
  s.m.cpu().regs().sr &= ~Cpu::kIMask;
  CHECK_EQ(s.wait_vector(100), 152u & 0x7F);
  CHECK_EQ(s.wait_vector(100), 152u & 0x7F);  // level sensitive: again until cleared
  // Clear protocol (fresh instance: the reads above already saw OVF = 1): read 1 then write 0.
  System s2;
  Bus& b2 = s2.bus();
  b2.write16(kIprh, 0x6000);
  b2.write16(kTcsr, 0x5AFE);
  const u64 t2 = s2.m.now();
  b2.write16(kTcsr, 0xA520);
  const u64 ovf2 = ((t2 >> 1) + 2) << 1;
  s2.m.run(8);
  CHECK(s2.m.intc().request(IrqSrc::Iti));
  b2.write16(kTcsr, 0xA520);  // write 0 without a prior read of 1: no effect
  CHECK(s2.m.intc().request(IrqSrc::Iti));
  CHECK_EQ(b2.read8(kTcsr), 0xB8);
  b2.write16(kTcsr, 0xA5A0);  // writing 1 does not clear
  CHECK_EQ(b2.read8(kTcsr), 0xB8);
  b2.write16(kTcsr, 0xA520);  // read 1 then write 0
  CHECK_EQ(b2.read8(kTcsr), 0x38);
  CHECK(!s2.m.intc().request(IrqSrc::Iti));
  // The next overflow comes 512 states after the previous one.
  // (run(1) can overshoot by a few states when the CPU is finishing an RTE, so
  // compare the flag against the clock at every step rather than at fixed points.)
  const u64 next = ovf2 + 512;
  for (int i = 0; i < 600; ++i) {
    s2.m.run(1);
    const u64 now = s2.m.now();
    CHECK_EQ((b2.read8(kTcsr) & 0x80) != 0, now >= next);
    if (now >= next) break;
    CHECK_EQ(b2.read8(kTcnt), u8((now >> 1) - (ovf2 >> 1)));
  }
  CHECK_EQ(b2.read8(kTcsr) & 0x80, 0x80);
  CHECK_EQ(s2.wait_vector(100), 152u & 0x7F);
  // IPRH level 0 masks it.
  b2.write16(kIprh, 0x0000);
  CHECK_EQ(s2.wait_vector(100), 0u);
  // TME = 0 stops and clears the counter; OVF stays until cleared by software.
  b2.read8(kTcsr);
  b2.write16(kTcsr, 0xA580);
  CHECK_EQ(b2.read8(kTcsr), 0x98);
  CHECK_EQ(b2.read8(kTcnt), 0);
  s2.m.run(100);
  CHECK_EQ(b2.read8(kTcnt), 0);
  b2.write16(kTcsr, 0xA500);
  CHECK_EQ(b2.read8(kTcsr), 0x18);
  CHECK_EQ(s2.m.sched().pending(), size_t(0));
}

// Watchdog mode: overflow sets WOVF, pulses WDTOVF, resets TCSR/TCNT and,
// with RSTE, calls the reset sink.
void test_watchdog_mode() {
  System s;
  Bus& b = s.bus();
  b.write16(kIprh, 0x6000);
  s.wdt.set_reset_sink(&s.sink);
  u64 pin_at = 0;
  int pulses = 0;
  s.wdt.set_wdtovf_callback([&](u64 at) { pin_at = at; ++pulses; });
  // RSTE = 0: WOVF and the pin only; TCNT/TCSR reset within the module (11.4.5).
  b.write16(kTcsr, 0x5AF8);
  const u64 t0 = s.m.now();
  b.write16(kTcsr, 0xA560);  // WT/IT, TME, phi/2
  const u64 ovf = ((t0 >> 1) + 8) << 1;
  s.m.run(40);
  CHECK_EQ(pulses, 1);
  CHECK_EQ(pin_at, ovf);
  CHECK(s.wdt.wdtovf_low(ovf));
  CHECK(s.wdt.wdtovf_low(ovf + 127));
  CHECK(!s.wdt.wdtovf_low(ovf + 128));
  CHECK(!s.wdt.wdtovf_low(ovf - 1));
  CHECK_EQ(s.wdt.rstcsr(), 0x9F);  // host peek: does not count as a read
  CHECK_EQ(b.read8(kTcsr), 0x18);  // timer stopped and cleared by the overflow
  CHECK_EQ(b.read8(kTcnt), 0x00);
  CHECK_EQ(s.sink.resets, 0);
  CHECK(!s.m.intc().request(IrqSrc::Iti));  // no ITI in watchdog mode
  CHECK_EQ(s.wait_vector(50), 0u);
  // WOVF: write H'A500 without reading 1 first does nothing; after a read it clears.
  b.write16(kRstcsrW, 0xA500);
  CHECK_EQ(s.wdt.rstcsr(), 0x9F);
  CHECK_EQ(b.read8(kRstcsrR), 0x9F);  // read 1 ...
  b.write16(kRstcsrW, 0xA500);        // ... then write 0
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  // RSTE = 1: the internal reset goes through the sink; RSTCSR survives it.
  b.write16(kRstcsrW, 0x5A40);
  CHECK_EQ(b.read8(kRstcsrR), 0x5F);
  b.write16(kTcsr, 0x5A00);
  const u64 t1 = s.m.now();
  b.write16(kTcsr, 0xA565);  // WT/IT, TME, phi/1024
  const u64 ovf2 = ((t1 >> 10) + 256) << 10;
  // Kicking the dog before the overflow postpones it.
  s.m.run(1024 * 200);
  CHECK_EQ(s.sink.resets, 0);
  b.write16(kTcsr, 0x5A00);
  const u64 t2 = s.m.now();
  const u64 ovf3 = ((t2 >> 10) + 256) << 10;
  s.m.run(ovf2 + 1024 - s.m.now());
  CHECK_EQ(s.sink.resets, 0);
  CHECK_EQ(b.read8(kRstcsrR), 0x5F);
  s.m.run(ovf3 - s.m.now() + 8);
  CHECK_EQ(s.sink.resets, 1);
  CHECK_EQ(pulses, 2);
  CHECK_EQ(pin_at, ovf3);
  CHECK(s.sink.at >= ovf3 && s.sink.at - ovf3 <= 2);
  CHECK_EQ(b.read8(kRstcsrR), 0xDF);  // WOVF and RSTE kept across the internal reset
  CHECK_EQ(b.read8(kTcsr), 0x18);
  CHECK_EQ(b.read8(kTcnt), 0x00);
  // A RES-pin reset clears RSTCSR too.
  s.wdt.reset();
  CHECK_EQ(b.read8(kRstcsrR), 0x1F);
  // Host-side accessors.
  b.write16(kTcsr, 0xA522);
  s.m.run(300);
  CHECK_EQ(s.wdt.tcnt(s.m.now()), b.read8(kTcnt));
  CHECK_EQ(s.wdt.tcsr(s.m.now()), 0x3A);
  CHECK_EQ(s.wdt.rstcsr(), 0x1F);
}

}  // namespace

int main() {
  test_register_protocol();
  test_count_rates();
  test_interval_mode();
  test_watchdog_mode();
  return test::finish("test_sh2_wdt");
}
