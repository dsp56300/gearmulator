// Watchdog timer tests (H8/510 manual section 16).
#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

constexpr u32 kTcsr = 0xFF10, kTcnt = 0xFF11, kRstcsrW = 0xFF1E, kRstcsrR = 0xFF1F;

struct Board {
  Machine m;
  Board() : m(ChipModel::H8_510, 2) { m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2); }
  Bus& bus() { return m.bus(); }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  u16 peek16(u32 addr) { return Bus::be16(m.bus().mem() + addr); }
  void start() {
    poke16(0, 0x0100);
    poke(0x0100, {0x00, 0x00, 0x20, 0xFC});
    for (u32 v = 0x10; v < 0x80; v += 2) {
      const u16 h = u16(0x0500 + (v - 0x10) * 4);
      poke16(v, h);
      poke(h, {0x58, 0x00, u8(v), 0x1D, 0xF1, 0x00, 0x90, 0x0A});
    }
    m.cpu().invalidate_all();
    m.reset();
    m.cpu().regs().r[7] = 0xF000;
    m.cpu().regs().sr &= u16(~Cpu::kMaskBits);
    bus().write8(0xFF00, 0x60);  // IRQ0 / WDT priority 6
  }
  u16 marker() { return peek16(0xF100); }
  u16 wait_vector(u64 max, u64* at = nullptr) {
    poke16(0xF100, 0);
    for (u64 t = 0; t < max && marker() == 0; ++t) m.run(1);
    if (at) *at = m.now();
    return marker();
  }
};

void test_register_protocol() {
  Board b;
  b.start();
  Bus& bus = b.bus();
  CHECK_EQ(bus.read8(kTcsr), 0x18);
  CHECK_EQ(bus.read8(kTcnt), 0x00);
  CHECK_EQ(bus.read8(kRstcsrR), 0x3F);
  // Byte writes are ignored.
  bus.write8(kTcsr, 0xFF);
  bus.write8(kTcnt, 0x55);
  CHECK_EQ(bus.read8(kTcsr), 0x18);
  CHECK_EQ(bus.read8(kTcnt), 0x00);
  // Word writes need the password.
  bus.write16(kTcsr, 0x1207);         // wrong password
  CHECK_EQ(bus.read8(kTcsr), 0x18);
  bus.write16(kTcsr, 0x5A55);         // TCNT
  CHECK_EQ(bus.read8(kTcnt), 0x55);
  CHECK_EQ(bus.read8(kTcsr), 0x18);
  bus.write16(kTcsr, 0xA5C7);         // TCSR: OVF cannot be set; WT/IT, CKS take
  CHECK_EQ(bus.read8(kTcsr), 0x5F);
  bus.write16(kTcsr, 0xA500);
  CHECK_EQ(bus.read8(kTcsr), 0x18);
  // RSTCSR: RSTOE via H'5A, WRST clear via H'A500.
  bus.write16(kRstcsrW, 0x5AFF);
  CHECK_EQ(bus.read8(kRstcsrR), 0x7F);
  bus.write16(kRstcsrW, 0x5A00);
  CHECK_EQ(bus.read8(kRstcsrR), 0x3F);
  // Through the CPU: MOV.W #H'5A12,@H'FF10:16 must land as one word.
  b.poke(0x0200, {0x1D, 0xFF, 0x10, 0x07, 0x5A, 0x12});
  b.m.cpu().regs().pc = 0x0200;
  b.m.cpu().step();
  CHECK_EQ(bus.read8(kTcnt), 0x12);
}

// Interval mode: TME = 1, WT/IT = 0.  With phi/2 the counter overflows every
// 512 states and requests the WDT interrupt (vector H'42) until OVF is cleared.
void test_interval_mode() {
  Board b;
  b.start();
  b.bus().write16(kTcsr, 0xA520);  // TME, interval, phi/2
  u64 at = 0;
  CHECK_EQ(b.wait_vector(1000, &at), 0x42);
  CHECK(at >= 512 && at <= 512 + 40);
  CHECK((b.bus().read8(kTcsr) & Wdt::kOvf) != 0);
  CHECK(b.m.intc().request(IrqSrc::Wdt));
  // Clear OVF: read as 1 (done above) then write 0.
  b.bus().write16(kTcsr, 0xA520);
  CHECK((b.bus().read8(kTcsr) & Wdt::kOvf) == 0);
  CHECK(!b.m.intc().request(IrqSrc::Wdt));
  // Next overflow a period later.
  u64 at2 = 0;
  CHECK_EQ(b.wait_vector(1000, &at2), 0x42);
  CHECK(at2 - at >= 512 - 40 && at2 - at <= 512 + 40);
  // Stopping the timer zeroes and holds the counter.
  b.bus().write16(kTcsr, 0xA500);
  CHECK_EQ(b.bus().read8(kTcnt), 0);
  b.m.run(2000);
  CHECK_EQ(b.bus().read8(kTcnt), 0);
  CHECK_EQ(b.m.resets(), 1u);  // only the start() reset
}

// Watchdog mode: an overflow resets the chip, WRST is set and survives the
// reset; the program restarts from the reset vector.  Refreshing TCNT
// (writing H'00) in time prevents it.
void test_watchdog_mode() {
  Board b;
  b.start();
  // Main loop increments R1 forever; refresh nothing -> reset expected.
  b.poke(0x0100, {0xA9, 0x08, 0x20, 0xFC});  // ADD:Q.W #1,R1 ; BRA -4
  b.m.cpu().invalidate_all();
  b.bus().write16(kTcsr, 0xA560);  // TME, watchdog mode, phi/2
  CHECK_EQ(b.m.resets(), 1u);
  b.m.run(600);
  CHECK_EQ(b.m.resets(), 2u);
  CHECK_EQ(b.bus().read8(kRstcsrR) & Wdt::kWrst, Wdt::kWrst);
  CHECK_EQ(b.bus().read8(kTcsr), 0x18);       // TCSR re-initialised: timer stopped
  CHECK_EQ(b.m.cpu().regs().pc & 0xFFFC, 0x0100u);  // restarted from the vector
  CHECK_EQ(b.m.cpu().interrupt_mask(), 7);
  // Software clears WRST.
  b.bus().write16(kRstcsrW, 0xA500);
  CHECK_EQ(b.bus().read8(kRstcsrR) & Wdt::kWrst, 0);
  // Refreshing the counter keeps the dog quiet.
  b.bus().write16(kTcsr, 0xA560);
  for (int i = 0; i < 10; ++i) {
    b.m.run(300);
    b.bus().write16(kTcsr, 0x5A00);
  }
  CHECK_EQ(b.m.resets(), 2u);
}

}  // namespace

int main() {
  test_register_protocol();
  test_interval_mode();
  test_watchdog_mode();
  return test::finish("test_wdt");
}
