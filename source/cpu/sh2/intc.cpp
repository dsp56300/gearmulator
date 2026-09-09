#include "cpu/sh2/intc.hpp"

namespace sh2 {

namespace {
using S = IrqSrc;
constexpr size_t N = size_t(IrqSrc::kCount);

struct Entry { IrqSrc src; u8 vector, ipr, shift; };

constexpr IntcLayout make_layout(const Entry* entries, size_t n, u32 ipr_base, u8 ipr_count, u32 icr, u32 isr,
                                 u16 icr_mask, std::array<u8, 8> pin_bit, std::array<IrqSrc, 8> pin_src) {
  IntcLayout l{};
  for (size_t i = 0; i < N; ++i) l.src[i] = {0, 0, 0, 0xFF};
  for (size_t i = 0; i < n; ++i) l.src[size_t(entries[i].src)] = {entries[i].vector, entries[i].ipr, entries[i].shift, u8(i)};
  l.ipr_base = ipr_base;
  l.ipr_count = ipr_count;
  l.icr = icr;
  l.isr = isr;
  l.icr_mask = icr_mask;
  l.pin_bit = pin_bit;
  l.pin_src = pin_src;
  return l;
}

// SH7014 table 6.3 in default priority order.  ADI is 136 on the SH7014,
// 138 on the SH7016/17.
constexpr Entry kSh7014[] = {
    {S::Irq0, 64, 0, 12}, {S::Irq1, 65, 0, 8}, {S::Irq2, 66, 0, 4}, {S::Irq3, 67, 0, 0},
    {S::Irq6, 70, 1, 4}, {S::Irq7, 71, 1, 0},
    {S::Dei0, 72, 2, 12}, {S::Dei1, 76, 2, 8},
    {S::Tgi0a, 88, 3, 12}, {S::Tgi0b, 89, 3, 12}, {S::Tgi0c, 90, 3, 12}, {S::Tgi0d, 91, 3, 12}, {S::Tci0v, 92, 3, 8},
    {S::Tgi1a, 96, 3, 4}, {S::Tgi1b, 97, 3, 4}, {S::Tci1v, 100, 3, 0}, {S::Tci1u, 101, 3, 0},
    {S::Tgi2a, 104, 4, 12}, {S::Tgi2b, 105, 4, 12}, {S::Tci2v, 108, 4, 8}, {S::Tci2u, 109, 4, 8},
    {S::Eri0, 128, 5, 4}, {S::Rxi0, 129, 5, 4}, {S::Txi0, 130, 5, 4}, {S::Tei0, 131, 5, 4},
    {S::Eri1, 132, 5, 0}, {S::Rxi1, 133, 5, 0}, {S::Txi1, 134, 5, 0}, {S::Tei1, 135, 5, 0},
    {S::Adi, 136, 6, 12},
    {S::Cmi0, 144, 6, 4}, {S::Cmi1, 148, 6, 0},
    {S::Iti, 152, 7, 12}, {S::Cmi, 153, 7, 12},
};
constexpr Entry kSh7016[] = {
    {S::Irq0, 64, 0, 12}, {S::Irq1, 65, 0, 8}, {S::Irq2, 66, 0, 4}, {S::Irq3, 67, 0, 0},
    {S::Irq6, 70, 1, 4}, {S::Irq7, 71, 1, 0},
    {S::Dei0, 72, 2, 12}, {S::Dei1, 76, 2, 8},
    {S::Tgi0a, 88, 3, 12}, {S::Tgi0b, 89, 3, 12}, {S::Tgi0c, 90, 3, 12}, {S::Tgi0d, 91, 3, 12}, {S::Tci0v, 92, 3, 8},
    {S::Tgi1a, 96, 3, 4}, {S::Tgi1b, 97, 3, 4}, {S::Tci1v, 100, 3, 0}, {S::Tci1u, 101, 3, 0},
    {S::Tgi2a, 104, 4, 12}, {S::Tgi2b, 105, 4, 12}, {S::Tci2v, 108, 4, 8}, {S::Tci2u, 109, 4, 8},
    {S::Eri0, 128, 5, 4}, {S::Rxi0, 129, 5, 4}, {S::Txi0, 130, 5, 4}, {S::Tei0, 131, 5, 4},
    {S::Eri1, 132, 5, 0}, {S::Rxi1, 133, 5, 0}, {S::Txi1, 134, 5, 0}, {S::Tei1, 135, 5, 0},
    {S::Adi, 138, 6, 12},
    {S::Cmi0, 144, 6, 4}, {S::Cmi1, 148, 6, 0},
    {S::Iti, 152, 7, 12}, {S::Cmi, 153, 7, 12},
};
// SH7034 table 5.3 / 5.4: IPRA-IPRE at H'5FFFF84, ICR at H'5FFFF8E with
// IRQ0S..IRQ7S in bits 7..0, no ISR.  The ITU's IMIAn / IMIBn / OVIn are the
// TGInA / TGInB / TCInV names.
constexpr Entry kSh7034[] = {
    {S::Irq0, 64, 0, 12}, {S::Irq1, 65, 0, 8}, {S::Irq2, 66, 0, 4}, {S::Irq3, 67, 0, 0},
    {S::Irq4, 68, 1, 12}, {S::Irq5, 69, 1, 8}, {S::Irq6, 70, 1, 4}, {S::Irq7, 71, 1, 0},
    {S::Dei0, 72, 2, 12}, {S::Dei1, 74, 2, 12}, {S::Dei2, 76, 2, 8}, {S::Dei3, 78, 2, 8},
    {S::Tgi0a, 80, 2, 4}, {S::Tgi0b, 81, 2, 4}, {S::Tci0v, 82, 2, 4},
    {S::Tgi1a, 84, 2, 0}, {S::Tgi1b, 85, 2, 0}, {S::Tci1v, 86, 2, 0},
    {S::Tgi2a, 88, 3, 12}, {S::Tgi2b, 89, 3, 12}, {S::Tci2v, 90, 3, 12},
    {S::Tgi3a, 92, 3, 8}, {S::Tgi3b, 93, 3, 8}, {S::Tci3v, 94, 3, 8},
    {S::Tgi4a, 96, 3, 4}, {S::Tgi4b, 97, 3, 4}, {S::Tci4v, 98, 3, 4},
    {S::Eri0, 100, 3, 0}, {S::Rxi0, 101, 3, 0}, {S::Txi0, 102, 3, 0}, {S::Tei0, 103, 3, 0},
    {S::Eri1, 104, 4, 12}, {S::Rxi1, 105, 4, 12}, {S::Txi1, 106, 4, 12}, {S::Tei1, 107, 4, 12},
    {S::Pei, 108, 4, 8}, {S::Adi, 109, 4, 8},
    {S::Iti, 112, 4, 4}, {S::Cmi, 113, 4, 4},
};

// SH7040 table 6.3 / 6.4: the SH7014 set plus IRQ4/5, DMAC2/3, MTU3/4, the
// second A/D unit (A mask: ADI1 137), the DTC (SWDTEND 140) and the MTU output
// enable (OEI 156).
constexpr Entry kSh7042[] = {
    {S::Irq0, 64, 0, 12}, {S::Irq1, 65, 0, 8}, {S::Irq2, 66, 0, 4}, {S::Irq3, 67, 0, 0},
    {S::Irq4, 68, 1, 12}, {S::Irq5, 69, 1, 8}, {S::Irq6, 70, 1, 4}, {S::Irq7, 71, 1, 0},
    {S::Dei0, 72, 2, 12}, {S::Dei1, 76, 2, 8}, {S::Dei2, 80, 2, 4}, {S::Dei3, 84, 2, 0},
    {S::Tgi0a, 88, 3, 12}, {S::Tgi0b, 89, 3, 12}, {S::Tgi0c, 90, 3, 12}, {S::Tgi0d, 91, 3, 12}, {S::Tci0v, 92, 3, 8},
    {S::Tgi1a, 96, 3, 4}, {S::Tgi1b, 97, 3, 4}, {S::Tci1v, 100, 3, 0}, {S::Tci1u, 101, 3, 0},
    {S::Tgi2a, 104, 4, 12}, {S::Tgi2b, 105, 4, 12}, {S::Tci2v, 108, 4, 8}, {S::Tci2u, 109, 4, 8},
    {S::Tgi3a, 112, 4, 4}, {S::Tgi3b, 113, 4, 4}, {S::Tgi3c, 114, 4, 4}, {S::Tgi3d, 115, 4, 4}, {S::Tci3v, 116, 4, 0},
    {S::Tgi4a, 120, 5, 12}, {S::Tgi4b, 121, 5, 12}, {S::Tgi4c, 122, 5, 12}, {S::Tgi4d, 123, 5, 12}, {S::Tci4v, 124, 5, 8},
    {S::Eri0, 128, 5, 4}, {S::Rxi0, 129, 5, 4}, {S::Txi0, 130, 5, 4}, {S::Tei0, 131, 5, 4},
    {S::Eri1, 132, 5, 0}, {S::Rxi1, 133, 5, 0}, {S::Txi1, 134, 5, 0}, {S::Tei1, 135, 5, 0},
    {S::Adi, 136, 6, 12}, {S::Adi1, 137, 6, 12}, {S::Swdtend, 140, 6, 8},
    {S::Cmi0, 144, 6, 4}, {S::Cmi1, 148, 6, 0},
    {S::Iti, 152, 7, 12}, {S::Cmi, 153, 7, 12}, {S::Oei, 156, 7, 8},
};

constexpr u8 kNoPin = 0xFF;
constexpr IntcLayout kLayoutSh7014 = make_layout(kSh7014, sizeof kSh7014 / sizeof kSh7014[0], 0xFFFF8348u, 8,
                                                 0xFFFF8358u, 0xFFFF835Au, 0x01F3, {7, 6, 5, 4, kNoPin, kNoPin, 1, 0},
                                                 {S::Irq0, S::Irq1, S::Irq2, S::Irq3, S::Irq0, S::Irq0, S::Irq6, S::Irq7});
constexpr IntcLayout kLayoutSh7016 = make_layout(kSh7016, sizeof kSh7016 / sizeof kSh7016[0], 0xFFFF8348u, 8,
                                                 0xFFFF8358u, 0xFFFF835Au, 0x01F3, {7, 6, 5, 4, kNoPin, kNoPin, 1, 0},
                                                 {S::Irq0, S::Irq1, S::Irq2, S::Irq3, S::Irq0, S::Irq0, S::Irq6, S::Irq7});
constexpr IntcLayout kLayoutSh7042 = make_layout(kSh7042, sizeof kSh7042 / sizeof kSh7042[0], 0xFFFF8348u, 8,
                                                 0xFFFF8358u, 0xFFFF835Au, 0x01FF, {7, 6, 5, 4, 3, 2, 1, 0},
                                                 {S::Irq0, S::Irq1, S::Irq2, S::Irq3, S::Irq4, S::Irq5, S::Irq6, S::Irq7});
constexpr IntcLayout kLayoutSh7034 = make_layout(kSh7034, sizeof kSh7034 / sizeof kSh7034[0], 0x05FFFF84u, 5,
                                                 0x05FFFF8Eu, 0, 0x01FF, {7, 6, 5, 4, 3, 2, 1, 0},
                                                 {S::Irq0, S::Irq1, S::Irq2, S::Irq3, S::Irq4, S::Irq5, S::Irq6, S::Irq7});
}  // namespace

const IntcLayout& intc_layout(ChipModel model) {
  switch (model) {
    case ChipModel::SH7014: return kLayoutSh7014;
    case ChipModel::SH7034: return kLayoutSh7034;
    case ChipModel::SH7042: return kLayoutSh7042;
    default: return kLayoutSh7016;
  }
}

Intc::Intc(Cpu& cpu, const IntcLayout& layout) : cpu_(cpu), layout_(layout) {
  cpu_.set_irq_ack_sink(this);
  reset();
}

void Intc::map(emu::IoMux& mux) {
  mux.assign(layout_.ipr_base, u32(layout_.ipr_count) * 2, this);
  mux.assign(layout_.icr, 2, this);
  if (layout_.isr) mux.assign(layout_.isr, 2, this);
}

void Intc::reset() {
  ipr_.fill(0);
  icr_ = 0;
  isr_ = 0;
  req_.fill(false);
  dma_route_.fill(false);
  update();
}

// ---- requests -----------------------------------------------------------------

void Intc::set_request(IrqSrc src, bool active) {
  const size_t i = size_t(src);
  if (req_[i] == active) return;
  req_[i] = active;
  if (active && dma_route_[i] && dma_) dma_->dma_request(src);
  update();
}

void Intc::set_irq_pin(unsigned n, bool low) {
  if (n >= 8 || layout_.pin_bit[n] == 0xFF) return;
  const bool was_low = pin_low_[n];
  pin_low_[n] = low;
  const u16 bit = pin_mask(n);
  if (icr_ & bit) {  // edge sensing: a falling edge is latched until accepted / cleared
    if (low && !was_low) isr_ |= bit;
  } else {
    if (low) isr_ |= bit; else isr_ &= u16(~bit);
  }
  req_[size_t(layout_.pin_src[n])] = (isr_ & bit) != 0;
  update();
}

bool Intc::set_nmi_pin(bool high) {
  const bool rising = (icr_ & 0x0100) != 0;
  const bool fire = high != nmi_high_ && high == rising;
  if (fire) cpu_.request_nmi();
  nmi_high_ = high;
  return fire;
}

// Highest level among the active requests, ties by the chip's default order;
// masked (level 0) and DMAC-routed sources are not presented to the CPU.
void Intc::update() {
  u8 best_level = 0, best_vector = 0, best_rank = 0xFF;
  bool best_pin = false;
  for (size_t i = 0; i < size_t(IrqSrc::kCount); ++i) {
    if (!req_[i] || dma_route_[i]) continue;
    const IrqSrcInfo& info = layout_.src[i];
    if (!info.vector) continue;
    const u8 level = u8((ipr_[info.ipr] >> info.shift) & 0xF);
    if (level > best_level || (level == best_level && level && info.rank < best_rank)) {
      best_level = level;
      best_vector = info.vector;
      best_rank = info.rank;
      best_pin = info.vector >= 64 && info.vector <= 71;
    }
  }
  cpu_.set_irq(best_level, best_vector, best_pin);
}

void Intc::irq_acknowledged(u8 vector) {
  // An accepted edge-detected IRQ pin request is withdrawn (figure 6.2).
  for (unsigned n = 0; n < 8; ++n) {
    const u16 bit = pin_mask(n);
    if (!bit || layout_.src[size_t(layout_.pin_src[n])].vector != vector) continue;
    if (icr_ & bit) {
      isr_ &= u16(~bit);
      req_[size_t(layout_.pin_src[n])] = false;
    }
  }
  update();
}

// Level-sensed pins: the request follows the pin.
void Intc::resample_pins() {
  for (unsigned n = 0; n < 8; ++n) {
    const u16 bit = pin_mask(n);
    if (!bit || (icr_ & bit)) continue;
    if (pin_low_[n]) isr_ |= bit; else isr_ &= u16(~bit);
    req_[size_t(layout_.pin_src[n])] = pin_low_[n];
  }
}

// ---- registers --------------------------------------------------------------

u16 Intc::read16(u32 addr) {
  if (addr >= layout_.ipr_base && addr < layout_.ipr_base + u32(layout_.ipr_count) * 2) return ipr_[(addr - layout_.ipr_base) >> 1];
  if (addr == layout_.icr) return icr();
  if (layout_.isr && addr == layout_.isr) return isr_;
  return 0;
}

void Intc::write16(u32 addr, u16 value) {
  if (addr >= layout_.ipr_base && addr < layout_.ipr_base + u32(layout_.ipr_count) * 2) {
    ipr_[(addr - layout_.ipr_base) >> 1] = value;
    update();
    return;
  }
  if (addr == layout_.icr) {
    icr_ = u16(value & layout_.icr_mask);
    resample_pins();
    update();
    return;
  }
  if (layout_.isr && addr == layout_.isr) {
    // Writing 0 to a flag read as 1 withdraws an edge-latched request.
    const u16 clear = u16(isr_ & ~value);
    for (unsigned n = 0; n < 8; ++n) {
      const u16 bit = pin_mask(n);
      if (bit && (clear & bit) && (icr_ & bit)) {
        isr_ &= u16(~bit);
        req_[size_t(layout_.pin_src[n])] = false;
      }
    }
    update();
  }
}

u8 Intc::read8(u32 addr) {
  const u16 w = read16(addr & ~1u);
  return u8((addr & 1) ? w : w >> 8);
}

void Intc::write8(u32 addr, u8 value) {
  const u32 base = addr & ~1u;
  const u16 old = read16(base);
  const u16 w = (addr & 1) ? u16((old & 0xFF00) | value) : u16((old & 0x00FF) | (u16(value) << 8));
  write16(base, w);
}

}  // namespace sh2
