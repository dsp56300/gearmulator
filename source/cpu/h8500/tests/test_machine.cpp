// Machine-level tests: bus devices, scheduler, event-driven interrupts.
#include <vector>

#include "cpu/h8500/machine.hpp"
#include "common/test_util.hpp"

using namespace h8500;

namespace {

struct Board {
  Machine m;
  explicit Board(ChipModel model = ChipModel::H8_510, u8 mode = 2) : m(model, mode) {
    // External RAM everywhere the chip does not already map something.
    m.bus().map_ram(0x0000, 0xFE80, BusClass::W16_S2);
  }
  void poke(u32 addr, std::initializer_list<u8> bytes) {
    u32 a = addr;
    for (u8 b : bytes) m.bus().mem()[a++] = b;
  }
  void poke16(u32 addr, u16 v) { Bus::put_be16(m.bus().mem() + addr, v); }
  void start(u16 pc, u16 sp = 0xF000) {
    poke16(0, pc);
    m.cpu().invalidate_all();
    m.reset();
    m.cpu().regs().r[7] = sp;
  }
};

// A register block that records accesses.
struct Probe : Device {
  std::vector<u32> reads, writes;
  u8 value = 0x5A;
  u8 read8(u32 a) override { reads.push_back(a); return value; }
  void write8(u32 a, u8 v) override { writes.push_back(a); value = v; }
};

void test_scheduler_order_and_cancel() {
  emu::Scheduler s;
  std::vector<int> fired;
  auto cb = [](void* ctx, u64, u64) { static_cast<std::vector<int>*>(ctx)->push_back(1); };
  auto cb2 = [](void* ctx, u64, u64) { static_cast<std::vector<int>*>(ctx)->push_back(2); };
  auto cb3 = [](void* ctx, u64, u64) { static_cast<std::vector<int>*>(ctx)->push_back(3); };
  const auto a = s.schedule(300, cb3, &fired);
  s.schedule(100, cb, &fired);
  s.schedule(200, cb2, &fired);
  CHECK_EQ(s.next_time(), 100u);
  s.cancel(a);
  s.run_due(250);
  CHECK_EQ(fired.size(), 2u);
  CHECK_EQ(fired[0], 1);
  CHECK_EQ(fired[1], 2);
  CHECK(s.empty());
  CHECK_EQ(s.next_time(), emu::Scheduler::kNever);
}

void test_mmio_device() {
  Board b;
  Probe p;
  b.m.bus().map_device(0xFE80, 0x80, &p, BusClass::W8_S3);
  b.poke(0x0100, {
      0x15, 0xFE, 0x80, 0x80,  // MOV.B @H'FE80:16,R0
      0x15, 0xFE, 0x81, 0x90,  // MOV.B R0,@H'FE81:16
  });
  b.start(0x0100);
  // 6 (@aa:16) + 1 (even start, table 2-9b) + 1 (I=1 byte, 8-bit 3-state) = 8 states
  CHECK_EQ(b.m.cpu().step(), 8u);
  CHECK_EQ(b.m.cpu().regs().r[0] & 0xFF, 0x5A);
  CHECK_EQ(p.reads.size(), 1u);
  CHECK_EQ(p.reads[0], 0xFE80u);
  b.m.cpu().regs().r[0] = 0x00A5;
  CHECK_EQ(b.m.cpu().step(), 8u);
  CHECK_EQ(p.writes.size(), 1u);
  CHECK_EQ(p.value, 0xA5);
  // ROM ignores writes; unmapped space reads H'FF.
  Bus& bus = b.m.bus();
  bus.map_rom(0xFF00, 0x80, BusClass::W16_S2);
  bus.mem()[0xFF00] = 0x12;
  bus.write8(0xFF00, 0x34);
  CHECK_EQ(bus.read8(0xFF00), 0x12);
  Bus other(16);
  CHECK_EQ(other.read16(0x1234), 0xFFFFu);
  other.write16(0x1234, 0);
  CHECK_EQ(other.read16(0x1234), 0xFFFFu);
}

// A timer event asserts IRQ0 at state 1001.  The CPU must take it at the end
// of the instruction in flight, and the scheduler must observe exact time.
// The handler acknowledges by writing a register that deasserts the request.
struct IrqSource : Device {
  Cpu* cpu;
  u64 seen_now = 0, seen_when = 0, accepted_at = 0;
  explicit IrqSource(Cpu* c) : cpu(c) {}
  u8 read8(u32) override { return 0; }
  void write8(u32, u8) override { accepted_at = cpu->total_states(); cpu->set_irq(0, 0); }
  static void fire(void* c, u64 when, u64 now) {
    auto* s = static_cast<IrqSource*>(c);
    s->seen_when = when;
    s->seen_now = now;
    s->cpu->set_irq(2, 32);
  }
};

void test_timed_interrupt() {
  Board b;
  IrqSource src(&b.m.cpu());
  b.m.bus().map_device(0xFE80, 0x80, &src, BusClass::W8_S3);
  b.poke16(0x0040, 0x0500);           // IRQ0 vector
  b.poke(0x0100, {0x00, 0x00, 0x00, 0x00, 0x20, 0xFA});  // 4 x NOP ; BRA -6   (2*4 + 7 = 15 states per lap)
  b.poke(0x0500, {0x15, 0xFE, 0x80, 0x90, 0x0A});        // MOV.B R0,@H'FE80:16 (ack) ; RTE
  b.start(0x0100);
  b.m.cpu().regs().sr &= u16(~Cpu::kMaskBits);
  b.m.sched().schedule(1001, &IrqSource::fire, &src);

  const u64 used = b.m.run(2000);
  CHECK(used >= 2000);
  CHECK_EQ(src.seen_when, 1001u);
  // The event fires once the instruction spanning state 1001 has finished:
  // no earlier than 1001 and at most one instruction (7 states) later.
  CHECK(src.seen_now >= 1001 && src.seen_now <= 1001 + 7);
  // The interrupt is accepted at that same boundary: the acknowledge (first
  // handler instruction) starts exactly one interrupt entry later, and a
  // device reading the clock mid-instruction sees that instruction's start.
  CHECK_EQ(src.accepted_at, src.seen_now + Cpu::kIrqStatesMin);
  CHECK_EQ(b.m.cpu().exceptions_taken(), 1u);
  CHECK(!b.m.cpu().sleeping());
  const u64 before = b.m.cpu().exceptions_taken();
  b.m.run(1000);
  CHECK_EQ(b.m.cpu().exceptions_taken(), before);
}

// Multiple events in one run() call, each seen at its own time.
void test_event_slicing() {
  Board b;
  b.poke(0x0100, {0x00, 0x20, 0xFD});  // NOP ; BRA -3
  b.start(0x0100);
  std::vector<u64> times;
  struct Ctx { std::vector<u64>* t; Machine* m; } ctx{&times, &b.m};
  auto cb = [](void* c, u64, u64 now) { static_cast<Ctx*>(c)->t->push_back(now); };
  for (u64 t = 50; t <= 500; t += 50) b.m.sched().schedule(t, cb, &ctx);
  b.m.run(600);
  CHECK_EQ(times.size(), 10u);
  for (size_t i = 0; i < times.size(); ++i) {
    const u64 want = 50 * (i + 1);
    CHECK(times[i] >= want && times[i] < want + 7);
  }
}

}  // namespace

int main() {
  test_scheduler_order_and_cancel();
  test_mmio_device();
  test_timed_interrupt();
  test_event_slicing();
  return test::finish("test_machine");
}
