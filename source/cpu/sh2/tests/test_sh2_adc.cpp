// SH7014-family A/D converters (manual sections 13 and 14).
#include "cpu/sh2/adc.hpp"
#include "cpu/sh2/machine.hpp"
#include "common/test_util.hpp"

using namespace sh2;

namespace {

constexpr u32 kIprg = 0xFFFF8354u;  // A/D priority: bits 15-12

// Same harness as test_sh2_intc: RAM at 0 for vectors, every handler writes
// its vector number (low 7 bits) to H'F100, a NOP loop in on-chip RAM.
struct System {
  Machine m;
  static constexpr u32 kRam = 0xFFFFF000u;
  explicit System(ChipModel model) : m(model, 1) {
    m.bus().map_ram(0x00000000, 0x10000, Bus::kClsCs0);
    for (u32 v = 64; v < 160; ++v) {
      const u32 h = 0x1000 + (v - 64) * 16;
      m.bus().write32(v * 4, h);
      const u16 code[] = {u16(0xE000 | (v & 0x7F)), 0x2102, 0x002B, 0x0009};  // MOV #v,R0; MOV.L R0,@R1; RTE; NOP
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
    m.cpu().regs().sr &= ~Cpu::kIMask;
    m.cpu().regs().r[1] = 0xF100;
    m.bus().write32(0xF100, 0);
    m.bus().write16(kIprg, 0xF000);  // A/D level 15
  }
  Bus& bus() { return m.bus(); }
  u64 now() const { return m.now(); }
  // Advance the clock without executing code: the converter is evaluated
  // lazily on register access, so this gives state-exact timing checks.
  void advance(u64 states) { m.cpu().stall(states); }
  u32 marker() { return bus().read32(0xF100); }
  u32 wait_vector(u64 max) {
    bus().write32(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    return marker();
  }
};

// ===========================================================================
// High speed A/D converter (SH7014, section 13)

constexpr u32 kHsCsr = 0xFFFF83E0u, kHsCr = 0xFFFF83E1u, kHsAddr = 0xFFFF83F0u;

struct Hs : System {
  AdcHighSpeed adc;
  Hs() : System(ChipModel::SH7014), adc(m.sched(), m.cpu(), m.intc()) {
    adc.map(m.io());
    adc.reset();
  }
  // High-speed start mode with the analog circuit already powered up.
  void power_up() {
    bus().write8(kHsCr, AdcHighSpeed::kPwr);
    advance(AdcHighSpeed::kPowerUp);
  }
  u8 csr() { return bus().read8(kHsCsr); }
  u16 addr(unsigned i) { return bus().read16(kHsAddr + 2 * i); }
};

void test_hs_reset_and_registers() {
  Hs s;
  Bus& b = s.bus();
  CHECK_EQ(b.read8(kHsCsr), 0x00);
  CHECK_EQ(b.read8(kHsCr), 0x00);
  for (unsigned i = 0; i < 8; ++i) CHECK_EQ(s.addr(i), 0x0000);
  CHECK_EQ(b.read16(kHsCsr), 0x0000);  // ADCSR:ADCR as a word
  CHECK_EQ(b.read8(0xFFFF83E2), 0xFF);  // reserved, unassigned
  // ADCR bit 7 is reserved and reads 0; the rest is read/write.
  b.write8(kHsCr, 0xFF);
  CHECK_EQ(b.read8(kHsCr), 0x7F);
  b.write8(kHsCr, 0x00);
  // ADCSR: ADF cannot be set by software; other bits stick (no ADST here).
  b.write8(kHsCsr, 0xDF);
  CHECK_EQ(b.read8(kHsCsr), 0x5F);
  b.write16(kHsCsr, 0x0000);
  CHECK_EQ(b.read16(kHsCsr), 0x0000);
  // Data registers are read-only.
  b.write16(kHsAddr, 0x1234);
  b.write8(kHsAddr + 3, 0x56);
  CHECK_EQ(s.addr(0), 0x0000);
  CHECK_EQ(s.addr(1), 0x0000);
  CHECK(!s.adc.converting());
}

void test_hs_single_timing() {
  Hs s;
  Bus& b = s.bus();
  s.power_up();
  s.adc.set_input(1, 0x2AB);
  s.adc.set_input(0, 0x3FF);
  const u64 t0 = s.now();
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 1);  // select-single, AN1
  CHECK_EQ(s.csr(), 0x21);
  CHECK(s.adc.converting());
  // The input is held tD + tSPL = 21.5 -> 22 states after the start.
  s.advance(21);
  s.adc.set_input(1, 0x155);  // still before the hold instant
  s.advance(1);
  s.adc.set_input(1, 0x0F0);  // after it: ignored
  // tCONV = 42.5 states: nothing at 42, done at 43.
  s.advance(42 - 22);
  CHECK_EQ(s.now(), t0 + 42);
  CHECK_EQ(s.csr(), 0x21);
  CHECK_EQ(s.addr(1), 0x0000);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x81);  // ADF set, ADST cleared
  CHECK_EQ(s.addr(1), 0x0155);  // right-justified
  CHECK_EQ(b.read8(kHsAddr + 2), 0x01);  // AD9-8 in the high byte
  CHECK_EQ(b.read8(kHsAddr + 3), 0x55);  // AD7-0 in the low byte
  CHECK_EQ(s.addr(0), 0x0000);  // other channels untouched
  CHECK(!s.adc.converting());
  CHECK(s.adc.powered());  // PWR = 1: analog stays on
  // ADF clearing: writing 1 never sets or clears it (and ends the read-1
  // condition); a write of 0 without a preceding read of ADF = 1 keeps it.
  b.write8(kHsCsr, 0x80);
  b.write8(kHsCsr, 0x00);
  CHECK_EQ(s.csr(), 0x80);
  // Read (ADF = 1) then write 0: cleared.
  b.write8(kHsCsr, 0x00);
  CHECK_EQ(s.csr(), 0x00);
  // A restart while powered starts at once again (no 200-state delay).
  const u64 t1 = s.now();
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 0);
  s.advance(42);
  CHECK_EQ(s.csr(), 0x20);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x80);
  CHECK_EQ(s.addr(0), 0x03FF);
  CHECK_EQ(s.now(), t1 + 43);
}

void test_hs_cks1() {
  Hs s;
  s.power_up();
  s.adc.set_input(7, 0x123);
  s.adc.set_sampler(nullptr);
  unsigned samples = 0;
  s.adc.set_sampler([&](unsigned ch) { ++samples; return u16(ch == 7 ? 0x123 : 0); });
  s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kCks | 7);
  s.advance(41);
  s.csr();
  CHECK_EQ(samples, 0u);
  s.advance(1);  // hold at 41.5 -> 42
  s.csr();
  CHECK_EQ(samples, 1u);
  s.advance(82 - 42);
  CHECK_EQ(s.csr(), 0x37);
  s.advance(1);  // tCONV = 82.5 -> 83
  CHECK_EQ(s.csr(), 0x97);
  CHECK_EQ(s.addr(7), 0x0123);
  CHECK_EQ(samples, 1u);
}

void test_hs_low_power_mode() {
  Hs s;
  Bus& b = s.bus();
  s.adc.set_input(2, 0x0AA);
  CHECK(!s.adc.powered());
  // PWR = 0: the analog circuit is powered with ADST and needs 200 states.
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 2);
  CHECK(s.adc.powered());
  s.advance(242);
  CHECK_EQ(s.csr(), 0x22);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x82);
  CHECK_EQ(s.addr(2), 0x00AA);
  CHECK(!s.adc.powered());  // switched off again at the end
  // PWR set together with ADST: the first conversion also waits 200 states,
  // and the circuit stays powered afterwards.
  b.write8(kHsCsr, 0x00);
  b.write8(kHsCr, AdcHighSpeed::kPwr);
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 2);
  s.advance(242);
  CHECK_EQ(s.csr(), 0x22);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x82);
  CHECK(s.adc.powered());
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 2);
  s.advance(43);
  CHECK_EQ(s.csr(), 0x82);
  // Clearing PWR while halted switches the circuit off.
  b.write8(kHsCr, 0x00);
  CHECK(!s.adc.powered());
}

void test_hs_interrupt() {
  Hs s;
  Bus& b = s.bus();
  s.power_up();
  b.write8(kHsCsr, AdcHighSpeed::kAdie | AdcHighSpeed::kAdst | 3);
  CHECK_EQ(s.wait_vector(200), 136u & 0x7F);
  CHECK(s.m.intc().request(IrqSrc::Adi));
  // Level-sensitive: taken again while ADF && ADIE.
  CHECK_EQ(s.wait_vector(100), 136u & 0x7F);
  // Clear ADF (read then write 0): request withdrawn.
  CHECK_EQ(b.read8(kHsCsr) & 0xE0, 0xC0);
  b.write8(kHsCsr, AdcHighSpeed::kAdie | 3);
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  CHECK_EQ(s.wait_vector(200), 0u);
  // ADIE cleared while ADF set: no request; set again: request.
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 3);
  s.advance(43);
  CHECK_EQ(s.csr(), 0x83);
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  b.write8(kHsCsr, AdcHighSpeed::kAdie | 3);  // ADF stays (no clear after this read? it was read: cleared)
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 3);
  s.advance(43);
  b.write8(kHsCsr, AdcHighSpeed::kAdie | 3);  // not read as 1 since the write: ADF kept
  CHECK_EQ(s.csr(), 0xC3);
  CHECK(s.m.intc().request(IrqSrc::Adi));
}

void test_hs_group_single() {
  Hs s;
  s.power_up();
  s.adc.set_input(0, 0x100);
  s.adc.set_input(1, 0x200);
  s.adc.set_input(2, 0x300);
  s.adc.set_input(3, 0x3FF);
  std::vector<unsigned> order;
  s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return u16(0x100 * (ch + 1)); });
  s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 2);  // AN0-AN2
  // 42.5 for the first channel, then 20 each (sampling overlaps the previous conversion).
  s.advance(42);
  CHECK_EQ(s.addr(0), 0x0000);
  s.advance(1);
  CHECK_EQ(s.addr(0), 0x0100);
  CHECK_EQ(s.addr(1), 0x0000);
  CHECK_EQ(s.csr(), 0x2A);
  CHECK_EQ(order.size(), 2u);  // AN1 held at the end of AN0's conversion
  s.advance(19);
  CHECK_EQ(s.addr(1), 0x0000);
  s.advance(1);
  CHECK_EQ(s.addr(1), 0x0200);
  CHECK_EQ(order.size(), 3u);
  s.advance(19);
  CHECK_EQ(s.csr(), 0x2A);
  s.advance(1);
  CHECK_EQ(s.addr(2), 0x0300);
  CHECK_EQ(s.addr(3), 0x0000);  // AN3 not in the group
  CHECK_EQ(s.csr(), 0x8A);  // ADF set, ADST cleared after the whole group
  CHECK_EQ(order.size(), 3u);
  CHECK_EQ(order[0], 0u);
  CHECK_EQ(order[1], 1u);
  CHECK_EQ(order[2], 2u);
}

void test_hs_scan() {
  Hs s;
  Bus& b = s.bus();
  s.power_up();
  unsigned samples = 0;
  u16 value = 0x010;
  s.adc.set_sampler([&](unsigned) { ++samples; return value; });
  b.write8(kHsCr, AdcHighSpeed::kPwr | AdcHighSpeed::kScan);
  // Select-scan, ADIE = 0: AN3 converted back to back; ADF after each conversion.
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 3);
  s.advance(43);
  CHECK_EQ(s.csr(), 0xA3);  // ADF set, ADST kept
  CHECK_EQ(s.addr(3), 0x0010);
  value = 0x020;
  s.advance(19);
  CHECK_EQ(s.addr(3), 0x0010);
  s.advance(1);  // 63: second conversion (its input was held at 43)
  CHECK_EQ(s.addr(3), 0x0010);
  s.advance(20);  // 83: the third, held at 63 -> new value
  CHECK_EQ(s.addr(3), 0x0020);
  CHECK_EQ(samples, 4u);  // 3 done, the 4th being converted
  // Clearing ADST stops the converter.
  b.write8(kHsCsr, 0x03);
  CHECK_EQ(s.csr() & 0x20, 0x00);
  s.advance(200);
  CHECK_EQ(samples, 4u);
  CHECK(!s.adc.converting());
  // Group-scan: AN0, AN1 rounds; ADF at the end of each round.
  b.write8(kHsCsr, 0x00);  // clear ADF (read above)
  samples = 0;
  const u64 t0 = s.now();
  b.write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 1);
  s.advance(43);
  CHECK_EQ(s.csr(), 0x29);
  s.advance(20);
  CHECK_EQ(s.csr(), 0xA9);
  CHECK_EQ(s.now(), t0 + 63);
  value = 0x030;  // changed after the hold instant of the next AN0 (= 63)
  s.advance(20);  // 83: AN0 again, held at 63 -> old value
  CHECK_EQ(s.addr(0), 0x0020);
  s.advance(20);  // 103: AN1, held at 83 -> new value
  CHECK_EQ(s.addr(1), 0x0030);
  b.write8(kHsCsr, 0x00);
  // Scan with ADIE = 1 (13.5): paused when ADF is set, resumed when it is cleared.
  b.write8(kHsCsr, 0x00);  // ADF clear (read-1 happened in csr())
  samples = 0;
  const u64 t1 = s.now();
  b.write8(kHsCsr, AdcHighSpeed::kAdie | AdcHighSpeed::kAdst | 5);
  s.advance(43);
  CHECK_EQ(s.csr(), 0xE5);
  CHECK(s.m.intc().request(IrqSrc::Adi));
  CHECK_EQ(samples, 1u);
  s.advance(300);
  CHECK_EQ(s.csr(), 0xE5);
  CHECK_EQ(samples, 1u);  // paused
  CHECK(!s.adc.converting());
  const u64 t2 = s.now();
  CHECK(t2 > t1 + 300);
  b.write8(kHsCsr, AdcHighSpeed::kAdie | AdcHighSpeed::kAdst | 5);  // ADF cleared: resumes
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  CHECK(s.adc.converting());
  s.advance(42);
  CHECK_EQ(s.csr(), 0x65);
  s.advance(1);
  CHECK_EQ(s.csr(), 0xE5);
  CHECK_EQ(s.now(), t2 + 43);
  CHECK_EQ(samples, 2u);
  // Clearing ADST while paused halts it for good.
  b.write8(kHsCsr, AdcHighSpeed::kAdie | 5);
  CHECK_EQ(s.csr(), 0x45);
  s.advance(100);
  CHECK_EQ(samples, 2u);
}

void test_hs_simultaneous_sampling() {
  Hs s;
  Bus& b = s.bus();
  s.power_up();
  std::vector<unsigned> order;
  s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return u16(0x10 * (ch + 1)); });
  b.write8(kHsCr, AdcHighSpeed::kPwr | AdcHighSpeed::kDsmp);
  b.write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 3);  // AN0,AN1 -> AN2,AN3
  s.advance(21);
  s.csr();
  CHECK_EQ(order.size(), 0u);
  s.advance(1);  // both channels of the pair held at 22
  s.csr();
  CHECK_EQ(order.size(), 2u);
  s.advance(21);  // 43: AN0
  CHECK_EQ(s.addr(0), 0x0010);
  CHECK_EQ(s.addr(1), 0x0000);
  s.advance(20);  // 63: AN1
  CHECK_EQ(s.addr(1), 0x0020);
  CHECK_EQ(order.size(), 2u);
  s.advance(19);  // 82: the next pair is still sampling
  s.csr();
  CHECK_EQ(order.size(), 2u);
  s.advance(1);  // 83 = 63 + tSPL
  s.csr();
  CHECK_EQ(order.size(), 4u);
  CHECK_EQ(order[2], 2u);
  CHECK_EQ(order[3], 3u);
  s.advance(20);  // 103: AN2
  CHECK_EQ(s.addr(2), 0x0030);
  CHECK_EQ(s.csr(), 0x2B);
  s.advance(20);  // 123: AN3, round done
  CHECK_EQ(s.addr(3), 0x0040);
  CHECK_EQ(s.csr(), 0x8B);
  CHECK_EQ(order.size(), 4u);
}

void test_hs_buffer_operation() {
  {
    // BUFE = 01, group mode, CH = 001: AN0 twice, result -> ADDRA -> ADDRB (table 13.4).
    Hs s;
    s.power_up();
    u16 seq = 0x11;
    std::vector<unsigned> order;
    s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return seq++; });
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr | 0x01);
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 1);
    s.advance(43);
    CHECK_EQ(s.addr(0), 0x0011);
    CHECK_EQ(s.addr(1), 0x0000);
    CHECK_EQ(s.csr(), 0x29);
    s.advance(20);
    CHECK_EQ(s.addr(0), 0x0012);
    CHECK_EQ(s.addr(1), 0x0011);
    CHECK_EQ(s.csr(), 0x89);
    CHECK_EQ(order.size(), 2u);
    CHECK_EQ(order[0], 0u);
    CHECK_EQ(order[1], 0u);
  }
  {
    // BUFE = 11, select-single mode, CH = 011: one stage per start, ADF after
    // four stages; results shift ADDRA -> ADDRB -> ADDRC -> ADDRD.
    Hs s;
    s.power_up();
    u16 seq = 1;
    s.adc.set_sampler([&](unsigned) { return seq++; });
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr | 0x03);
    for (unsigned n = 1; n <= 3; ++n) {
      s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | 3);
      s.advance(43);
      CHECK_EQ(s.csr(), 0x03);  // ADST cleared, no ADF yet
      CHECK_EQ(s.addr(0), n);
    }
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | 3);
    s.advance(43);
    CHECK_EQ(s.csr(), 0x83);
    CHECK_EQ(s.addr(0), 4);
    CHECK_EQ(s.addr(1), 3);
    CHECK_EQ(s.addr(2), 2);
    CHECK_EQ(s.addr(3), 1);
    // The stage counter restarts: three more without ADF...
    s.bus().write8(kHsCsr, 0x03);
    for (unsigned n = 0; n < 3; ++n) {
      s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | 3);
      s.advance(43);
      CHECK_EQ(s.csr(), 0x03);
    }
    // ...and writing BUFE = 00 resets it (13.4.5 "Resetting the Number of Buffer Operations").
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr);
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr | 0x03);
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | 3);
    s.advance(43);
    CHECK_EQ(s.csr(), 0x03);
  }
  {
    // BUFE = 10, group mode, CH = 100: AN0, AN1, AN4 (table 13.5); ADDRC/ADDRD buffer ADDRA/ADDRB.
    Hs s;
    s.power_up();
    std::vector<unsigned> order;
    s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return u16(0x100 + ch); });
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr | 0x02);
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 4);
    s.advance(43 + 20 + 20);
    CHECK_EQ(s.csr(), 0x8C);
    CHECK_EQ(order.size(), 3u);
    CHECK_EQ(order[0], 0u);
    CHECK_EQ(order[1], 1u);
    CHECK_EQ(order[2], 4u);
    CHECK_EQ(s.addr(0), 0x0100);
    CHECK_EQ(s.addr(1), 0x0101);
    CHECK_EQ(s.addr(2), 0x0000);
    CHECK_EQ(s.addr(3), 0x0000);
    CHECK_EQ(s.addr(4), 0x0104);
    // Second round: the previous AN0/AN1 results move to ADDRC/ADDRD.
    s.adc.set_sampler([&](unsigned ch) { return u16(0x200 + ch); });
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 4);
    s.advance(83);
    CHECK_EQ(s.addr(0), 0x0200);
    CHECK_EQ(s.addr(1), 0x0201);
    CHECK_EQ(s.addr(2), 0x0100);
    CHECK_EQ(s.addr(3), 0x0101);
    CHECK_EQ(s.addr(4), 0x0204);
  }
  {
    // BUFE = 11, group mode, CH = 110: AN0, AN4-AN6 stored in ADDRA, ADDRE-G;
    // ADDRA-ADDRC before the start move to ADDRB-ADDRD (13.4.5).
    Hs s;
    s.power_up();
    std::vector<unsigned> order;
    s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return u16(0x300 + ch); });
    s.bus().write8(kHsCr, AdcHighSpeed::kPwr | 0x03);
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 6);
    s.advance(43 + 3 * 20);
    CHECK_EQ(s.csr(), 0x8E);
    CHECK_EQ(order.size(), 4u);
    CHECK_EQ(order[1], 4u);
    CHECK_EQ(order[3], 6u);
    CHECK_EQ(s.addr(0), 0x0300);
    CHECK_EQ(s.addr(4), 0x0304);
    CHECK_EQ(s.addr(6), 0x0306);
    CHECK_EQ(s.addr(7), 0x0000);
    s.adc.set_sampler([&](unsigned ch) { return u16(0x380 + ch); });
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 6);
    s.advance(103);
    CHECK_EQ(s.addr(0), 0x0380);
    CHECK_EQ(s.addr(1), 0x0300);
    CHECK_EQ(s.addr(2), 0x0000);
    s.bus().write8(kHsCsr, AdcHighSpeed::kAdst | AdcHighSpeed::kGrp | 6);
    s.advance(103);
    CHECK_EQ(s.addr(0), 0x0380);
    CHECK_EQ(s.addr(1), 0x0380);
    CHECK_EQ(s.addr(2), 0x0300);
    CHECK_EQ(s.addr(3), 0x0000);
  }
}

void test_hs_trigger() {
  Hs s;
  Bus& b = s.bus();
  s.power_up();
  s.adc.set_input(6, 0x0C6);
  b.write8(kHsCsr, 6);
  // TRGS = 00: the MTU trigger is ignored.
  s.adc.trigger();
  CHECK_EQ(s.csr(), 0x06);
  s.advance(100);
  CHECK_EQ(s.addr(6), 0x0000);
  // TRGS = 01: the trigger sets ADST and the conversion runs as usual.
  b.write8(kHsCr, AdcHighSpeed::kPwr | AdcHighSpeed::kTrgsMtu);
  const u64 t0 = s.now();
  s.adc.trigger();
  CHECK_EQ(s.csr(), 0x26);
  s.adc.trigger();  // while converting: ignored
  s.advance(42);
  CHECK_EQ(s.csr(), 0x26);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x86);
  CHECK_EQ(s.addr(6), 0x00C6);
  CHECK_EQ(s.now(), t0 + 43);
  // Software start still works with TRGS = 01.
  b.write8(kHsCsr, AdcHighSpeed::kAdst | 6);
  CHECK_EQ(s.csr(), 0x26);
  s.advance(43);
  CHECK_EQ(s.csr(), 0x86);
  // Reserved TRGS = 1x: no trigger start.
  b.write8(kHsCsr, 0x06);
  b.write8(kHsCr, AdcHighSpeed::kPwr | 0x20);
  s.adc.trigger();
  CHECK_EQ(s.csr(), 0x06);
}

void test_hs_dmac_clear() {
  Hs s;
  s.power_up();
  s.bus().write8(kHsCsr, AdcHighSpeed::kAdie | AdcHighSpeed::kAdst | 0);
  s.advance(43);
  s.adc.dmac_activated();  // (syncs: the conversion end is replayed)
  CHECK(s.m.intc().request(IrqSrc::Adi));  // until the data register is read
  s.advance(10);
  CHECK(s.m.intc().request(IrqSrc::Adi));
  s.addr(0);
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  CHECK_EQ(s.csr(), 0x40);
}

// ===========================================================================
// Mid-speed A/D converter (SH7016/SH7017, section 14)

constexpr u32 kMsAddr = 0xFFFF8420u, kMsCsr = 0xFFFF8428u, kMsCr = 0xFFFF8429u;

struct Ms : System {
  AdcMidSpeed adc;
  Ms() : System(ChipModel::SH7016), adc(m.sched(), m.cpu(), m.intc()) {
    adc.map(m.io());
    adc.reset();
  }
  u8 csr() { return bus().read8(kMsCsr); }
  u16 addr(unsigned i) { return bus().read16(kMsAddr + 2 * i); }
};

void test_ms_reset_and_registers() {
  Ms s;
  Bus& b = s.bus();
  CHECK_EQ(b.read8(kMsCsr), 0x00);
  CHECK_EQ(b.read8(kMsCr), 0x7F);  // TRGE = 0, reserved bits read 1
  CHECK_EQ(b.read16(kMsCsr), 0x007F);
  for (unsigned i = 0; i < 4; ++i) CHECK_EQ(s.addr(i), 0x0000);
  b.write8(kMsCr, 0x80);
  CHECK_EQ(b.read8(kMsCr), 0xFF);
  b.write8(kMsCr, 0x00);
  CHECK_EQ(b.read8(kMsCr), 0x7F);
  b.write8(kMsCsr, 0xDF);  // ADF not settable; no ADST
  CHECK_EQ(b.read8(kMsCsr), 0x5F);
  b.write8(kMsCsr, 0x00);
  b.write16(kMsAddr, 0xFFFF);  // read-only
  CHECK_EQ(s.addr(0), 0x0000);
  CHECK_EQ(b.read8(0xFFFF842A), 0xFF);  // reserved
}

void test_ms_single_timing() {
  Ms s;
  Bus& b = s.bus();
  s.adc.set_input(5, 0x2AB);
  const u64 t0 = s.now();
  b.write8(kMsCsr, AdcMidSpeed::kAdst | 5);  // AN5 -> ADDRB
  CHECK_EQ(s.csr(), 0x25);
  // Held at tD + tSPL = 17 + 64 = 81 states.
  s.advance(80);
  s.adc.set_input(5, 0x155);
  s.advance(1);
  s.adc.set_input(5, 0x000);
  // tCONV = 266 states (table 14.4 maximum).
  s.advance(265 - 81);
  CHECK_EQ(s.now(), t0 + 265);
  CHECK_EQ(s.csr(), 0x25);
  CHECK_EQ(s.addr(1), 0x0000);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x85);
  CHECK_EQ(s.addr(1), 0x5540);  // left-justified: AD9-2 | AD1-0 in bits 7-6
  CHECK_EQ(s.addr(0), 0x0000);
  CHECK(!s.adc.converting());
  // Byte reads: the upper byte latches the lower one into TEMP (14.3).
  CHECK_EQ(b.read8(kMsAddr + 2), 0x55);
  CHECK_EQ(b.read8(kMsAddr + 3), 0x40);
  b.write8(kMsCsr, 0x05);  // ADF read as 1 above: cleared
  CHECK_EQ(s.csr(), 0x05);
  s.adc.set_input(5, 0x3FF);
  b.write8(kMsCsr, AdcMidSpeed::kAdst | 5);
  s.advance(266);
  CHECK_EQ(b.read8(kMsAddr + 3), 0x40);  // stale TEMP: upper byte not read yet
  CHECK_EQ(b.read8(kMsAddr + 2), 0xFF);
  CHECK_EQ(b.read8(kMsAddr + 3), 0xC0);
  CHECK_EQ(s.addr(1), 0xFFC0);
  // CKS = 1: held at 9 + 32 = 41, done at 134.
  CHECK_EQ(s.csr(), 0x85);
  b.write8(kMsCsr, 0x00);
  unsigned samples = 0;
  s.adc.set_sampler([&](unsigned ch) { ++samples; return u16(ch); });
  b.write8(kMsCsr, AdcMidSpeed::kAdst | AdcMidSpeed::kCks | 2);
  s.advance(40);
  s.csr();
  CHECK_EQ(samples, 0u);
  s.advance(1);
  s.csr();
  CHECK_EQ(samples, 1u);
  s.advance(133 - 41);
  CHECK_EQ(s.csr(), 0x2A);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x8A);
  CHECK_EQ(s.addr(2), 0x0080);  // 2 << 6
  // Flag clearing protocol: writing 1 to ADF has no effect (and ends the
  // read-1 condition); a write of 0 without a preceding read of ADF = 1 keeps it.
  b.write8(kMsCsr, 0x8A);
  b.write8(kMsCsr, 0x0A);
  CHECK_EQ(s.csr(), 0x8A);
  b.write8(kMsCsr, 0x0A);
  CHECK_EQ(s.csr(), 0x0A);
}

void test_ms_interrupt() {
  Ms s;
  Bus& b = s.bus();
  b.write8(kMsCsr, AdcMidSpeed::kAdie | AdcMidSpeed::kAdst | 0);
  CHECK_EQ(s.wait_vector(600), 138u & 0x7F);
  CHECK_EQ(s.wait_vector(100), 138u & 0x7F);  // level: taken again
  CHECK_EQ(b.read8(kMsCsr) & 0xE0, 0xC0);
  b.write8(kMsCsr, AdcMidSpeed::kAdie);
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  CHECK_EQ(s.wait_vector(300), 0u);
}

void test_ms_scan() {
  Ms s;
  Bus& b = s.bus();
  std::vector<unsigned> order;
  u16 value = 0x001;
  s.adc.set_sampler([&](unsigned ch) { order.push_back(ch); return u16(value + ch); });
  // Group 1, three channels: AN4, AN5, AN6 -> ADDRA, ADDRB, ADDRC.
  const u64 t0 = s.now();
  b.write8(kMsCsr, AdcMidSpeed::kAdst | AdcMidSpeed::kScan | 6);
  s.advance(265);
  CHECK_EQ(s.addr(0), 0x0000);
  s.advance(1);  // 266: AN4
  CHECK_EQ(s.addr(0), u16(0x005 << 6));
  CHECK_EQ(s.csr(), 0x36);  // ADST kept, no ADF yet
  CHECK_EQ(order.size(), 1u);
  // Second and later conversions: 256 states, the input held after 64.
  s.advance(63);
  s.csr();
  CHECK_EQ(order.size(), 1u);
  s.advance(1);
  s.csr();
  CHECK_EQ(order.size(), 2u);
  CHECK_EQ(order[1], 5u);
  s.advance(255 - 64);
  CHECK_EQ(s.addr(1), 0x0000);
  s.advance(1);  // 522: AN5
  CHECK_EQ(s.addr(1), u16(0x006 << 6));
  CHECK_EQ(s.csr(), 0x36);
  s.advance(256);  // 778: AN6, round complete
  CHECK_EQ(s.addr(2), u16(0x007 << 6));
  CHECK_EQ(s.addr(3), 0x0000);
  CHECK_EQ(s.csr(), 0xB6);
  CHECK_EQ(s.now(), t0 + 778);
  // The next round starts at once with AN4 again (held at 778 + 64, done at 1034).
  value = 0x101;
  s.advance(256);
  CHECK_EQ(s.addr(0), u16(0x105 << 6));
  CHECK_EQ(order.size(), 4u);  // AN5 of this round not yet sampled (1034 + 64)
  CHECK_EQ(order[3], 4u);
  // ADST cleared: stops.
  b.write8(kMsCsr, AdcMidSpeed::kScan | 6);
  CHECK_EQ(s.csr(), 0x16);  // ADF cleared too (read as 1 before)
  s.advance(1000);
  CHECK_EQ(order.size(), 4u);
  CHECK(!s.adc.converting());
  // Group 0 scan with one channel: AN0 -> ADDRA each 256 states after the first.
  order.clear();
  b.write8(kMsCsr, AdcMidSpeed::kAdst | AdcMidSpeed::kScan | 0);
  s.advance(266);
  CHECK_EQ(s.csr(), 0xB0);
  CHECK_EQ(s.addr(0), u16(0x101 << 6));
  s.advance(256);
  CHECK_EQ(s.csr(), 0xB0);
  CHECK_EQ(order.size(), 2u);
  CHECK_EQ(order[1], 0u);
  s.advance(64);
  s.csr();
  CHECK_EQ(order.size(), 3u);
}

void test_ms_trigger() {
  Ms s;
  Bus& b = s.bus();
  s.adc.set_input(3, 0x0F0);
  b.write8(kMsCsr, 3);
  s.adc.trigger();  // TRGE = 0: ignored
  CHECK_EQ(s.csr(), 0x03);
  s.advance(300);
  CHECK_EQ(s.addr(3), 0x0000);
  b.write8(kMsCr, AdcMidSpeed::kTrge);
  const u64 t0 = s.now();
  s.adc.trigger();
  CHECK_EQ(s.csr(), 0x23);
  s.adc.trigger();  // busy: ignored
  s.advance(265);
  CHECK_EQ(s.csr(), 0x23);
  s.advance(1);
  CHECK_EQ(s.csr(), 0x83);
  CHECK_EQ(s.addr(3), u16(0x0F0 << 6));
  CHECK_EQ(s.now(), t0 + 266);
  // Trigger start in scan mode keeps running like a software start.
  b.write8(kMsCsr, AdcMidSpeed::kScan | 1);
  s.adc.trigger();
  s.advance(266 + 256);
  CHECK_EQ(s.csr(), 0xB1);
}

void test_ms_dmac_clear() {
  Ms s;
  s.bus().write8(kMsCsr, AdcMidSpeed::kAdie | AdcMidSpeed::kAdst | 1);
  s.advance(266);
  s.adc.dmac_activated();  // (syncs: the conversion end is replayed)
  CHECK(s.m.intc().request(IrqSrc::Adi));
  s.advance(10);
  CHECK(s.m.intc().request(IrqSrc::Adi));
  s.addr(1);  // any register access of the module clears ADF
  CHECK(!s.m.intc().request(IrqSrc::Adi));
  CHECK_EQ(s.csr(), 0x41);
}

}  // namespace

int main() {
  test_hs_reset_and_registers();
  test_hs_single_timing();
  test_hs_cks1();
  test_hs_low_power_mode();
  test_hs_interrupt();
  test_hs_group_single();
  test_hs_scan();
  test_hs_simultaneous_sampling();
  test_hs_buffer_operation();
  test_hs_trigger();
  test_hs_dmac_clear();
  test_ms_reset_and_registers();
  test_ms_single_timing();
  test_ms_interrupt();
  test_ms_scan();
  test_ms_trigger();
  test_ms_dmac_clear();
  return test::finish("test_sh2_adc");
}
