#include <bit>

#include "cpu/h8500/dtc.hpp"

namespace h8500 {

Dtc::Dtc(Bus& bus, Cpu& cpu, Intc& intc) : bus_(bus), cpu_(cpu), intc_(intc) {}

void Dtc::reset() { pending_ = 0; }

// Table 6-4 (minimum mode); maximum mode uses twice the address with the
// pointer in the second word.
u32 Dtc::vector_addr(IrqSrc src) {
  switch (src) {
    case IrqSrc::Irq0: return 0xC0;
    case IrqSrc::Irq1: return 0xC8;
    case IrqSrc::Irq2: return 0xCA;
    case IrqSrc::Irq3: return 0xCC;
    case IrqSrc::Frt1Ici: return 0xD0;
    case IrqSrc::Frt1Ocia: return 0xD2;
    case IrqSrc::Frt1Ocib: return 0xD4;
    case IrqSrc::Frt2Ici: return 0xD8;
    case IrqSrc::Frt2Ocia: return 0xDA;
    case IrqSrc::Frt2Ocib: return 0xDC;
    case IrqSrc::TmrCmia: return 0xE0;
    case IrqSrc::TmrCmib: return 0xE2;
    case IrqSrc::Sci1Rxi: return 0xEA;
    case IrqSrc::Sci1Txi: return 0xEC;
    case IrqSrc::Sci2Rxi: return 0xF2;
    case IrqSrc::Sci2Txi: return 0xF4;
    case IrqSrc::Adi: return 0xF8;
    default: return 0;
  }
}

bool Dtc::dtc_request(IrqSrc src, u8 vector) {
  if (!vector_addr(src)) return false;
  pending_ |= 1u << unsigned(src);
  cpu_.request_break();  // transfer at the next instruction boundary (Machine::run)
  vector_[size_t(src)] = vector;
  return true;
}

unsigned Dtc::access_states(u32 addr, bool word) const {
  const BusClass c = bus_.bus_class(addr);
  switch (c) {
    case BusClass::W16_S2: return 2;
    case BusClass::W16_S3: return word ? 3 : 3;
    case BusClass::W8_S2: return word ? 4 : 2;
    case BusClass::W8_S3: return word ? 6 : 3;
  }
  return 2;
}

void Dtc::service() {
  while (pending_) {
    // Sources in IrqSrc order = table 5-2 priority order.
    const unsigned s = unsigned(std::countr_zero(pending_));
    pending_ &= ~(1u << s);
    transfer(IrqSrc(s), vector_[s]);
  }
}

void Dtc::transfer(IrqSrc src, u8 vector) {
  const bool max = cpu_.max_mode();
  const u32 va = vector_addr(src);
  const u16 ta = bus_.read16(max ? (va * 2 + 2) : va);
  const u16 dtmr = bus_.read16(ta);
  u16 dtsr = bus_.read16(u16(ta + 2));
  u16 dtdr = bus_.read16(u16(ta + 4));
  u16 dtcr = bus_.read16(u16(ta + 6));
  const bool word = (dtmr & 0x8000) != 0;
  const bool si = (dtmr & 0x4000) != 0;
  const bool di = (dtmr & 0x2000) != 0;

  u16 data;
  if (word) {
    data = bus_.read16(dtsr);
    bus_.write16(dtdr, data);
  } else {
    data = bus_.read8(dtsr);
    bus_.write8(dtdr, u8(data));
  }
  unsigned states = 26 + access_states(dtsr, word) + access_states(dtdr, word);
  if (si) { dtsr = u16(dtsr + (word ? 2 : 1)); bus_.write16(u16(ta + 2), dtsr); states += 2; }
  if (di) { dtdr = u16(dtdr + (word ? 2 : 1)); bus_.write16(u16(ta + 4), dtdr); states += 2; }
  dtcr = u16(dtcr - 1);
  bus_.write16(u16(ta + 6), dtcr);
  cpu_.stall(states);
  ++transfers_;

  // The request is cleared: module flag for on-chip sources, the latched
  // pin request for IRQ1-IRQ3.
  if (clear_[size_t(src)]) clear_[size_t(src)](data);
  if (src == IrqSrc::Irq1 || src == IrqSrc::Irq2 || src == IrqSrc::Irq3) intc_.irq_acknowledged(vector);

  if (dtcr == 0) intc_.raise_cpu_interrupt(src);
}

}  // namespace h8500
