// SH7014 compare match timer (manual section 15).
#include "cpu/sh2/cmt.hpp"
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kCmstr = 0xFFFF83D0u, kCmcsr0 = 0xFFFF83D2u, kCmcnt0 = 0xFFFF83D4u, kCmcor0 = 0xFFFF83D6u;
constexpr u32 kCmcsr1 = 0xFFFF83D8u, kCmcnt1 = 0xFFFF83DAu, kCmcor1 = 0xFFFF83DCu;
constexpr u32 kIprg = 0xFFFF8354u;

struct System {
  Machine m;
  Cmt cmt;
  static constexpr u32 kRam = 0xFFFFF000u;
  System() : m(ChipModel::SH7014, 1), cmt(m.sched(), m.cpu(), m.intc()) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    cmt.map(m.io());
    // Vectors: every interrupt handler writes its vector number (low 7 bits) to H'F100 and returns.
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
    cmt.reset();
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

void test_reset_and_access() {
  System s;
  Bus& b = s.bus();
  CHECK_EQ(b.read16(kCmstr), 0x0000);
  CHECK_EQ(b.read16(kCmcsr0), 0x0000);
  CHECK_EQ(b.read16(kCmcnt0), 0x0000);
  CHECK_EQ(b.read16(kCmcor0), 0xFFFF);
  CHECK_EQ(b.read16(kCmcsr1), 0x0000);
  CHECK_EQ(b.read16(kCmcnt1), 0x0000);
  CHECK_EQ(b.read16(kCmcor1), 0xFFFF);
  CHECK_EQ(b.read32(kCmstr), 0u);  // CMSTR:CMCSR0 as one longword
  // Reserved bits read 0; CMF cannot be written 1.
  b.write16(kCmstr, 0xFFFF);
  CHECK_EQ(b.read16(kCmstr), 0x0003);
  b.write16(kCmstr, 0);
  b.write16(kCmcsr0, 0xFFFF);
  CHECK_EQ(b.read16(kCmcsr0), 0x0043);
  b.write16(kCmcsr0, 0);
  // Byte access: each half separately (15.5.3).
  b.write16(kCmcor1, 0x1234);
  b.write8(kCmcor1, 0xAB);
  CHECK_EQ(b.read16(kCmcor1), 0xAB34);
  b.write8(kCmcor1 + 1, 0xCD);
  CHECK_EQ(b.read8(kCmcor1), 0xAB);
  CHECK_EQ(b.read8(kCmcor1 + 1), 0xCD);
  b.write16(kCmcnt1, 0x5678);
  b.write8(kCmcnt1 + 1, 0x9A);
  CHECK_EQ(b.read16(kCmcnt1), 0x569A);
  b.write8(kCmcsr1 + 1, 0x42);  // CMIE + CKS = phi/128
  CHECK_EQ(b.read16(kCmcsr1), 0x0042);
  b.write8(kCmcsr1, 0xFF);      // reserved high byte
  CHECK_EQ(b.read16(kCmcsr1), 0x0042);
  // Longword write covers CMSTR and CMCSR0.
  b.write32(kCmstr, 0x00000041u);
  CHECK_EQ(b.read16(kCmstr), 0x0000);
  CHECK_EQ(b.read16(kCmcsr0), 0x0041);
}

// Count rate: the counter increments at every multiple of the divider while STR is set.
void test_count_rates() {
  System s;
  Bus& b = s.bus();
  // Channel 0 at phi/8, channel 1 at phi/512, both free (CMCOR = H'FFFF).
  b.write16(kCmcsr0, 0x0000);
  b.write16(kCmcsr1, 0x0003);
  const u64 t0 = s.m.now();
  b.write16(kCmstr, 0x0003);
  s.m.run(1000);
  u64 now = s.m.now();
  CHECK_EQ(b.read16(kCmcnt0), u16((now >> 3) - (t0 >> 3)));
  CHECK_EQ(b.read16(kCmcnt1), u16((now >> 9) - (t0 >> 9)));
  s.m.run(20000);
  now = s.m.now();
  CHECK_EQ(b.read16(kCmcnt0), u16((now >> 3) - (t0 >> 3)));
  CHECK_EQ(b.read16(kCmcnt1), u16((now >> 9) - (t0 >> 9)));
  CHECK(b.read16(kCmcnt1) >= 39);
  // Halting freezes the count; restarting resumes from it.
  const u16 held0 = b.read16(kCmcnt0);
  b.write16(kCmstr, 0x0002);
  s.m.run(500);
  CHECK_EQ(b.read16(kCmcnt0), held0);
  const u64 t1 = s.m.now();
  b.write16(kCmstr, 0x0003);
  s.m.run(800);
  now = s.m.now();
  CHECK_EQ(b.read16(kCmcnt0), u16(held0 + (now >> 3) - (t1 >> 3)));
  // Changing the divider keeps the count and continues at the new rate.
  const u16 held1 = b.read16(kCmcnt1);
  const u64 t2 = s.m.now();
  b.write16(kCmcsr1, 0x0001);  // phi/32
  s.m.run(3000);
  now = s.m.now();
  CHECK_EQ(b.read16(kCmcnt1), u16(held1 + (now >> 5) - (t2 >> 5)));
  // No flag while the counter has not reached CMCOR.
  CHECK_EQ(b.read16(kCmcsr0) & 0x80, 0);
  CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0);
}

// Compare match at the exact state: CMCOR = 9 at phi/8 matches on the tenth
// count after start (the counter clears on the count following the match, 15.4.2).
void test_match_timing_and_interrupt() {
  System s;
  Bus& b = s.bus();
  b.write16(kIprg, 0x0050);  // CMT0 level 5
  b.write16(kCmcor0, 9);
  b.write16(kCmcsr0, 0x0040);  // CMIE, phi/8
  s.m.cpu().regs().sr |= Cpu::kIMask;  // hold the interrupt off while probing the flag
  const u64 t0 = s.m.now();
  b.write16(kCmstr, 0x0001);
  const u64 match = ((t0 >> 3) + 10) << 3;
  // Walk up to the match one state at a time.
  u64 last_clear = 0, first_set = 0;
  for (int i = 0; i < 200 && !first_set; ++i) {
    s.m.run(1);
    const u64 now = s.m.now();
    const u16 csr = b.read16(kCmcsr0);
    if (csr & 0x80) first_set = now;
    else last_clear = now;
    if (!(csr & 0x80)) CHECK_EQ(b.read16(kCmcnt0), u16((now >> 3) - (t0 >> 3)));
  }
  CHECK(first_set != 0);
  CHECK(last_clear < match);
  CHECK(first_set >= match);
  CHECK(first_set - match <= 2);  // run(1) granularity of the NOP/BRA loop
  CHECK_EQ(b.read16(kCmcnt0), u16(((s.m.now() >> 3) - (t0 >> 3)) % 10));
  // CMI0 is presented as vector 144 once the mask drops.
  s.m.cpu().regs().sr &= ~Cpu::kIMask;
  CHECK_EQ(s.wait_vector(100), 144u & 0x7F);
  // Still requested (level sensitive) until CMF is cleared: read 1, write 0.
  CHECK_EQ(s.wait_vector(100), 144u & 0x7F);
  CHECK(s.m.intc().request(IrqSrc::Cmi0));
  // (The probing reads above saw CMF = 1.)  Writing 1 never clears; a write
  // consumes the read latch, so a following write of 0 has no effect either.
  b.write16(kCmcsr0, 0x00C0);
  CHECK_EQ(s.cmt.cmcsr(0, s.m.now()), 0x00C0);  // host peek: does not count as a read
  b.write16(kCmcsr0, 0x0040);
  CHECK_EQ(s.cmt.cmcsr(0, s.m.now()), 0x00C0);
  CHECK(s.m.intc().request(IrqSrc::Cmi0));
  CHECK_EQ(b.read16(kCmcsr0), 0x00C0);  // read 1 ...
  b.write16(kCmcsr0, 0x0040);           // ... then write 0: cleared
  CHECK_EQ(b.read16(kCmcsr0), 0x0040);
  CHECK(!s.m.intc().request(IrqSrc::Cmi0));
  // It comes back exactly one period (80 states) after the previous match.
  // (run(1) can overshoot by a few states when the CPU is finishing an RTE, so
  // compare the flag against the clock at every step rather than at fixed points.)
  const u64 next = match + 80 * ((s.m.now() - match) / 80 + 1);
  for (int i = 0; i < 200; ++i) {
    s.m.run(1);
    const u64 now = s.m.now();
    CHECK_EQ((b.read16(kCmcsr0) & 0x80) != 0, now >= next);
    if (now >= next) break;
  }
  CHECK_EQ(b.read16(kCmcsr0) & 0x80, 0x80);
  CHECK_EQ(s.wait_vector(100), 144u & 0x7F);
  // CMIE = 0: flag stays, request withdrawn.
  b.write16(kCmcsr0, 0x0080);
  CHECK_EQ(b.read16(kCmcsr0), 0x0080);
  CHECK(!s.m.intc().request(IrqSrc::Cmi0));
  CHECK_EQ(s.wait_vector(100), 0u);
  b.write16(kCmstr, 0);
}

// The counter clears at the match and keeps its period over several matches.
void test_period() {
  System s;
  Bus& b = s.bus();
  b.write16(kCmcor1, 4);       // period 5 counts
  b.write16(kCmcsr1, 0x0001);  // phi/32, no interrupt
  const u64 t0 = s.m.now();
  b.write16(kCmstr, 0x0002);
  for (int i = 0; i < 12; ++i) {
    s.m.run(97);
    const u64 ticks = (s.m.now() >> 5) - (t0 >> 5);
    CHECK_EQ(b.read16(kCmcnt1), u16(ticks % 5));
    const u16 csr = b.read16(kCmcsr1);
    CHECK_EQ((csr & 0x80) != 0, ticks >= 5);
  }
  CHECK(!s.m.intc().request(IrqSrc::Cmi1));
  // Clear CMF and check it sets again exactly one period later, counter at 0.
  b.write16(kCmcsr1, 0x0001);
  CHECK_EQ(b.read16(kCmcsr1), 0x0001);
  const u64 ticks = (s.m.now() >> 5) - (t0 >> 5);
  const u64 next = (((t0 >> 5) + ticks - ticks % 5 + 5) << 5);
  while (s.m.now() + 32 < next) s.m.run(1);
  // The state right before the match still shows CMF clear...
  while (s.m.now() < next - 1) s.m.run(1);
  if (s.m.now() < next) CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0);
  while (s.m.now() < next) s.m.run(1);
  CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0x80);
  CHECK_EQ(b.read16(kCmcnt1), u16(((s.m.now() >> 5) - (t0 >> 5)) % 5));
  // A CMCNT written above CMCOR wraps through H'FFFF and matches on the way up.
  b.write16(kCmcsr1, 0x0001);
  b.write16(kCmcnt1, 0xFFFE);
  const u64 t1 = s.m.now();
  s.m.run(32 * 3);
  CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0);
  const u16 v = b.read16(kCmcnt1);
  CHECK(v <= 2 || v == 0xFFFF);
  s.m.run(32 * 5);
  CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0x80);
  CHECK_EQ(b.read16(kCmcnt1), u16(((s.m.now() >> 5) - (t1 >> 5) - 7) % 5));
  // Channel 1 interrupt: vector 148 at IPRG bits 3-0.
  b.write16(kIprg, 0x0004);
  b.write16(kCmcsr1, 0x0041);  // CMIE (CMF stays set: not read as 1 before this write)
  CHECK_EQ(s.wait_vector(100), 148u & 0x7F);
  // CMCOR = 0: match on every count.
  b.write16(kCmstr, 0);
  b.write16(kCmcsr1, 0x0001);
  b.read16(kCmcsr1);
  b.write16(kCmcsr1, 0x0001);
  b.write16(kCmcnt1, 0);
  b.write16(kCmcor1, 0);
  b.write16(kCmstr, 0x0002);
  s.m.run(64);
  CHECK_EQ(b.read16(kCmcsr1) & 0x80, 0x80);
  CHECK_EQ(b.read16(kCmcnt1), 0);
}

// Host-side accessors and the scheduler stay quiet without an enabled interrupt.
void test_host_api() {
  System s;
  Bus& b = s.bus();
  b.write16(kCmcor0, 100);
  b.write16(kCmstr, 0x0001);
  CHECK_EQ(s.m.sched().pending(), size_t(0));  // no CMIE: nothing scheduled
  s.m.run(8 * 50);
  CHECK_EQ(s.cmt.cmcnt(0, s.m.now()), b.read16(kCmcnt0));
  CHECK_EQ(s.cmt.cmcor(0), 100);
  CHECK_EQ(s.cmt.cmstr(), 0x0001);
  b.write16(kCmcsr0, 0x0040);
  CHECK_EQ(s.m.sched().pending(), size_t(1));
  CHECK_EQ(s.cmt.cmcsr(0, s.m.now()), 0x0040);
}

}  // namespace

int main() {
  test_reset_and_access();
  test_count_rates();
  test_match_timing_and_interrupt();
  test_period();
  test_host_api();
  return test::finish("test_sh2_cmt");
}
