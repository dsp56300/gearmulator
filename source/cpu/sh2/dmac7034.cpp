#include "cpu/sh2/dmac7034.hpp"

namespace sh2 {

namespace {
constexpr IrqSrc kDei[Dmac7034::kChannels] = {IrqSrc::Dei0, IrqSrc::Dei1, IrqSrc::Dei2, IrqSrc::Dei3};
constexpr unsigned kOrder0[4] = {0, 3, 2, 1}, kOrder1[4] = {1, 3, 2, 0};
constexpr u32 kAddrMask = 0x0FFFFFFFu;
}  // namespace

Dmac7034::Dmac7034(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, Cpu& cpu)
    : sched_(sched), clock_(clock), intc_(intc), bus_(bus), cpu_(cpu) {
  intc_.set_dma_client(this);
  reset();
}

Dmac7034::~Dmac7034() {
  if (event_) sched_.cancel(event_);
  intc_.set_dma_client(nullptr);
}

void Dmac7034::map(emu::IoMux& mux) { mux.assign(kBase, 0x40, this); }

void Dmac7034::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  for (Channel& c : ch_) {
    c.sar = c.dar = 0;
    c.tcr = c.chcr = 0;
    c.te_read = c.dreq_edge = false;
  }
  dmaor_ = dmaor_read_ = 0;
  for (unsigned ch = 0; ch < kChannels; ++ch) update_irq(ch);
  update_routes();
}

// Table 9.3: RS3-RS0 -> on-chip module request.
bool Dmac7034::module_source(u16 chcr, IrqSrc& src) {
  switch ((chcr >> kRsShift) & 0xF) {
    case 0x4: src = IrqSrc::Rxi0; return true;
    case 0x5: src = IrqSrc::Txi0; return true;
    case 0x6: src = IrqSrc::Rxi1; return true;
    case 0x7: src = IrqSrc::Txi1; return true;
    case 0x8: src = IrqSrc::Tgi0a; return true;
    case 0x9: src = IrqSrc::Tgi1a; return true;
    case 0xA: src = IrqSrc::Tgi2a; return true;
    case 0xB: src = IrqSrc::Tgi3a; return true;
    case 0xD: src = IrqSrc::Adi; return true;
    default: return false;  // DREQ (0, 2, 3), auto-request (C), prohibited
  }
}

const unsigned* Dmac7034::order() const { return ((dmaor_ & kPrMask) >> 8) == 1 ? kOrder1 : kOrder0; }

bool Dmac7034::enabled(unsigned ch) const {
  const Channel& c = ch_[ch & 3];
  return (c.chcr & kDe) && !(c.chcr & kTe) && (dmaor_ & kDme) && !(dmaor_ & (kNmif | kAe));
}

bool Dmac7034::ready(unsigned ch) const {
  const Channel& c = ch_[ch];
  if (!enabled(ch)) return false;
  const unsigned rs = (c.chcr >> kRsShift) & 0xF;
  if (rs == 0xC) return true;
  if (ch < 2 && external(c.chcr)) return (c.chcr & kDs) ? c.dreq_edge : c.dreq_low;
  return false;
}

void Dmac7034::update_routes() {
  std::array<bool, size_t(IrqSrc::kCount)> want{};
  for (const Channel& c : ch_) {
    IrqSrc src;
    if ((c.chcr & kDe) && module_source(c.chcr, src)) want[size_t(src)] = true;
  }
  for (size_t i = 0; i < want.size(); ++i) {
    if (want[i] == routed_[i]) continue;
    routed_[i] = want[i];
    intc_.set_dma_route(IrqSrc(i), want[i]);
  }
}

void Dmac7034::update_irq(unsigned ch) {
  const Channel& c = ch_[ch];
  intc_.set_request(kDei[ch], (c.chcr & kTe) && (c.chcr & kIe));
}

void Dmac7034::kick() {
  update_routes();
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    IrqSrc src;
    if (enabled(ch) && module_source(ch_[ch].chcr, src) && intc_.request(src)) dma_request(src);
  }
  if (event_) return;
  for (unsigned ch = 0; ch < kChannels; ++ch)
    if (ready(ch)) { event_ = sched_.schedule(clock_.now() + kRequestLatency, &Dmac7034::on_event, this); return; }
}

void Dmac7034::on_event(void* self, u64 /*when*/, u64 /*now*/) {
  auto* d = static_cast<Dmac7034*>(self);
  d->event_ = 0;
  d->service();
}

void Dmac7034::service() {
  const unsigned* ord = order();
  for (unsigned i = 0; i < kChannels; ++i) {
    const unsigned ch = ord[i];
    if (!ready(ch)) continue;
    run_channel(ch);
    ch_[ch].dreq_edge = false;
    break;
  }
  for (unsigned ch = 0; ch < kChannels; ++ch)
    if (ready(ch)) { event_ = sched_.schedule(clock_.now() + kCycleStealGap, &Dmac7034::on_event, this); return; }
}

void Dmac7034::run_channel(unsigned ch) {
  if (burst(ch_[ch].chcr)) {
    while (transfer_unit(ch)) {}
  } else {
    transfer_unit(ch);
  }
}

void Dmac7034::dma_request(IrqSrc src) {
  bool served = false;
  for (unsigned ch = 0; ch < kChannels; ++ch) {
    IrqSrc s;
    if (!enabled(ch) || !module_source(ch_[ch].chcr, s) || s != src) continue;
    run_channel(ch);
    served = true;
  }
  if (served && clear_[size_t(src)]) clear_[size_t(src)]();
}

void Dmac7034::on_nmi() {
  dmaor_ |= kNmif;
  if (event_) { sched_.cancel(event_); event_ = 0; }
}

void Dmac7034::set_dreq(unsigned ch, bool low) {
  if (ch >= 2) return;
  Channel& c = ch_[ch];
  if (low && !c.dreq_low) c.dreq_edge = true;
  c.dreq_low = low;
  kick();
}

// ---------------------------------------------------------------------------
// Transfer

bool Dmac7034::address_error(u32 addr, unsigned bytes) const {
  if (addr & (bytes - 1)) return true;
  u32 off;
  return !bus_.translate(addr & kAddrMask, off);
}

bool Dmac7034::transfer_unit(unsigned ch) {
  Channel& c = ch_[ch];
  const u16 chcr = c.chcr;
  const unsigned bytes = (chcr & kTs) ? 2 : 1;
  const unsigned rs = (chcr >> kRsShift) & 0xF;
  const bool src_is_dack = rs == 3, dst_is_dack = rs == 2;
  const u32 amask = ~u32(bytes - 1) & kAddrMask;

  bool err = false;
  if (!src_is_dack && address_error(c.sar, bytes)) err = true;
  if (!dst_is_dack && address_error(c.dar, bytes)) err = true;

  u32 data;
  if (bytes == 2) data = bus_.read16(c.sar & amask); else data = bus_.read8(c.sar & kAddrMask);
  if (bytes == 2) bus_.write16(c.dar & amask, u16(data)); else bus_.write8(c.dar & kAddrMask, u8(data));

  u32 states = 0;
  if (!src_is_dack) states += cost(c.sar & kAddrMask, bytes);
  if (!dst_is_dack) states += cost(c.dar & kAddrMask, bytes);
  cpu_.stall(states);

  const unsigned sm = (chcr >> kSmShift) & 3, dm = (chcr >> kDmShift) & 3;
  if (!src_is_dack) c.sar += sm == 1 ? bytes : sm == 2 ? u32(0) - bytes : 0;
  if (!dst_is_dack) c.dar += dm == 1 ? bytes : dm == 2 ? u32(0) - bytes : 0;
  c.tcr = u16(c.tcr - 1);
  ++transfers_;

  if (err) {
    dmaor_ |= kAe;
    if (event_) { sched_.cancel(event_); event_ = 0; }
    cpu_.request_dma_address_error();
    return false;
  }
  if (c.tcr == 0) {
    c.chcr |= kTe;
    update_irq(ch);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Registers

u16 Dmac7034::read16(u32 addr) {
  const u32 off = addr - kBase;
  const unsigned ch = (off >> 4) & 3;
  Channel& c = ch_[ch];
  switch (off & 0xE) {
    case 0x0: return u16(c.sar >> 16);
    case 0x2: return u16(c.sar);
    case 0x4: return u16(c.dar >> 16);
    case 0x6: return u16(c.dar);
    case 0x8:
      if (ch != 0) return 0;
      dmaor_read_ |= u16(dmaor_ & (kAe | kNmif));
      return dmaor_;
    case 0xA: return c.tcr;
    case 0xE:
      if (c.chcr & kTe) c.te_read = true;
      return c.chcr;
    default: return 0;
  }
}

u32 Dmac7034::read32(u32 addr) { return (u32(read16(addr)) << 16) | read16(addr + 2); }

u8 Dmac7034::read8(u32 addr) {
  const u16 w = read16(addr & ~1u);
  return u8((addr & 1) ? w : w >> 8);
}

void Dmac7034::write16(u32 addr, u16 value) {
  const u32 off = addr - kBase;
  const unsigned ch = (off >> 4) & 3;
  Channel& c = ch_[ch];
  switch (off & 0xE) {
    case 0x0: c.sar = (c.sar & 0x0000FFFFu) | (u32(value) << 16); return;
    case 0x2: c.sar = (c.sar & 0xFFFF0000u) | value; return;
    case 0x4: c.dar = (c.dar & 0x0000FFFFu) | (u32(value) << 16); return;
    case 0x6: c.dar = (c.dar & 0xFFFF0000u) | value; return;
    case 0x8: if (ch == 0) write_dmaor(value); return;
    case 0xA: c.tcr = value; return;
    case 0xE: write_chcr(ch, value); return;
    default: return;
  }
}

void Dmac7034::write32(u32 addr, u32 value) {
  write16(addr, u16(value >> 16));
  write16(addr + 2, u16(value));
}

void Dmac7034::write8(u32 addr, u8 value) {
  const u32 base = addr & ~1u;
  const u16 old = read16(base);
  write16(base, (addr & 1) ? u16((old & 0xFF00) | value) : u16((old & 0x00FF) | (u16(value) << 8)));
}

void Dmac7034::write_chcr(unsigned ch, u16 value) {
  Channel& c = ch_[ch];
  bool te = (c.chcr & kTe) != 0;
  if (te && !(value & kTe) && c.te_read) te = false;  // read 1 then write 0
  c.te_read = false;
  c.chcr = u16((value & ~kTe) | (te ? kTe : 0));
  update_irq(ch);
  kick();
}

void Dmac7034::write_dmaor(u16 value) {
  u16 flags = u16(dmaor_ & (kAe | kNmif));
  flags &= u16(~(dmaor_read_ & ~value & (kAe | kNmif)));
  dmaor_read_ = 0;
  dmaor_ = u16((value & (kDme | kPrMask)) | flags);
  kick();
}

}  // namespace sh2
