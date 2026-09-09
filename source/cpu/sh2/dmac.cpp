#include "cpu/sh2/dmac.hpp"

namespace sh2 {

Dmac::Dmac(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, Bus& bus, Cpu& cpu, unsigned channels)
    : sched_(sched), clock_(clock), intc_(intc), bus_(bus), cpu_(cpu), channels_(channels),
      tcr_mask_(channels > 2 ? 0xFFFFFFu : 0xFFFFu) {
  intc_.set_dma_client(this);
  reset();
}

Dmac::~Dmac() {
  if (event_) sched_.cancel(event_);
  intc_.set_dma_client(nullptr);
}

void Dmac::map(emu::IoMux& mux) {
  mux.assign(kDmaor, 2, this);
  for (unsigned ch = 0; ch < channels_; ++ch) mux.assign(kChBase[ch], 0x10, this);
}

void Dmac::reset() {
  if (event_) sched_.cancel(event_);
  event_ = 0;
  for (Channel& c : ch_) {
    // SAR, DAR and DMATCR are undefined after a power-on reset (9.2.1-9.2.3):
    // modelled as 0.  The DREQ pin level is physical and survives the reset.
    c.sar = c.dar = c.tcr = 0;
    c.chcr = 0;
    c.te_read = false;
    c.dreq_edge = false;
  }
  dmaor_ = 0;
  dmaor_read_ = 0;
  for (unsigned ch = 0; ch < channels_; ++ch) update_irq(ch);
  update_routes();
}

// ---------------------------------------------------------------------------
// Request sources

// Table 9.4: RS3-RS0 -> on-chip module request signal.
bool Dmac::module_source(u32 chcr, IrqSrc& src) {
  switch ((chcr >> kRsShift) & 0xF) {
    case 0x6: src = IrqSrc::Tgi0a; return true;
    case 0x7: src = IrqSrc::Tgi1a; return true;
    case 0x8: src = IrqSrc::Tgi2a; return true;
    case 0x9: src = IrqSrc::Tgi3a; return true;  // SH7042 (prohibited on the SH7014)
    case 0xA: src = IrqSrc::Tgi4a; return true;
    case 0xB: src = IrqSrc::Adi; return true;
    case 0xC: src = IrqSrc::Txi0; return true;
    case 0xD: src = IrqSrc::Rxi0; return true;
    case 0xE: src = IrqSrc::Txi1; return true;
    case 0xF: src = IrqSrc::Rxi1; return true;
    default: return false;  // external (0, 2, 3), auto-request (4) or prohibited (1, 5, 9, A)
  }
}

// 9.3.1 step 1: DE = 1, DME = 1, TE = 0, NMIF = 0, AE = 0.
bool Dmac::enabled(unsigned ch) const {
  const Channel& c = ch_[ch & 3];
  return (c.chcr & kDe) && !(c.chcr & kTe) && (dmaor_ & kDme) && !(dmaor_ & (kNmif | kAe));
}

// Auto-request channels always hold a request; external ones while DREQ is
// low (level) or a falling edge is latched (edge).  Module requests are
// served synchronously by dma_request and never wait here.
bool Dmac::ready(unsigned ch) const {
  const Channel& c = ch_[ch];
  if (!enabled(ch)) return false;
  const unsigned rs = (c.chcr >> kRsShift) & 0xF;
  if (rs == 4) return true;
  if (external(c.chcr)) return (c.chcr & kDs) ? c.dreq_edge : c.dreq_low;
  return false;
}

// A module's interrupt request signal is a DMA request while a channel has DE
// set for it (9.3.2 "interrupts for the CPU are not generated").
void Dmac::update_routes() {
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

void Dmac::update_irq(unsigned ch) {
  static constexpr IrqSrc kDei[kMaxChannels] = {IrqSrc::Dei0, IrqSrc::Dei1, IrqSrc::Dei2, IrqSrc::Dei3};
  const Channel& c = ch_[ch];
  intc_.set_request(kDei[ch], (c.chcr & kTe) && (c.chcr & kIe));
}

bool Dmac::any_ready() const {
  for (unsigned ch = 0; ch < channels_; ++ch)
    if (ready(ch)) return true;
  return false;
}

// Re-examine every request after a register write, a pin change or a flag
// change: pending module requests are served now, auto / external ones are
// scheduled.
void Dmac::kick(bool routes) {
  if (routes) update_routes();
  for (unsigned ch = 0; ch < channels_; ++ch) {
    IrqSrc src;
    if (enabled(ch) && module_source(ch_[ch].chcr, src) && intc_.request(src)) dma_request(src);
  }
  if (event_) return;
  if (any_ready()) event_ = sched_.schedule(clock_.now() + kRequestLatency, &Dmac::on_event, this);
}

void Dmac::on_event(void* self, u64 /*when*/, u64 /*now*/) {
  auto* d = static_cast<Dmac*>(self);
  d->event_ = 0;
  d->service();
}

// One activation of the highest-priority ready channel (9.3.3), then the bus
// goes back to the CPU for kCycleStealGap states if anything is still waiting.
void Dmac::service() {
  for (unsigned ch = 0; ch < channels_; ++ch) {
    if (!ready(ch)) continue;
    Channel& c = ch_[ch];
    // DRAK: one pulse per accepted DREQ sampling (9.3.5).
    if (external(c.chcr)) pulse(drak_[ch], (c.chcr & kRl) != 0);
    run_channel(ch);
    // An edge request is consumed by the transfer it started: one unit in
    // cycle-steal mode, the whole block in burst mode (9.3.5 edge detection).
    c.dreq_edge = false;
    break;
  }
  if (any_ready()) event_ = sched_.schedule(clock_.now() + kCycleStealGap, &Dmac::on_event, this);
}

// Burst mode keeps the bus until the end condition (TE, AE) is reached; the
// burst is atomic here, so a DREQ negated during a level-detected burst or an
// NMI during the block is only seen afterwards (approximation of 9.3.4 /
// 9.3.6, where the DMAC stops after the current unit).
void Dmac::run_channel(unsigned ch) {
  if (burst(ch_[ch].chcr)) {
    while (transfer_unit(ch)) {}
  } else {
    transfer_unit(ch);
  }
}

void Dmac::dma_request(IrqSrc src) {
  bool served = false;
  for (unsigned ch = 0; ch < channels_; ++ch) {
    IrqSrc s;
    if (!enabled(ch) || !module_source(ch_[ch].chcr, s) || s != src) continue;
    run_channel(ch);
    served = true;
  }
  // The transfer discontinues the request (9.3.2): cycle steal after the
  // first unit, burst after the last one.
  if (served && clear_[size_t(src)]) clear_[size_t(src)]();
}

void Dmac::on_nmi() {
  dmaor_ |= kNmif;
  if (event_) { sched_.cancel(event_); event_ = 0; }
}

void Dmac::set_dreq(unsigned ch, bool low) {
  if (ch >= 2) return;  // DREQ0 / DREQ1 only
  Channel& c = ch_[ch & 3];
  if (low && !c.dreq_low) c.dreq_edge = true;
  c.dreq_low = low;
  // The module routes depend on CHCR alone; a pin change only re-examines the requests.
  kick(false);
}

// ---------------------------------------------------------------------------
// Transfer

// Table 5.5 for a DMAC bus cycle: word / longword misaligned, longword in the
// 8-bit peripheral line; plus anything outside the decoded windows (reserved
// space).  Single-chip-mode external accesses are not checked.
bool Dmac::address_error(u32 addr, unsigned bytes) const {
  if (addr & (bytes - 1)) return true;
  u32 off;
  if (!bus_.translate(addr, off)) return true;
  if (bytes == 4 && (bus_.attr(addr) & Bus::kClassMask) == Bus::kClsPeriph8) return true;
  return false;
}

bool Dmac::transfer_unit(unsigned ch) {
  Channel& c = ch_[ch];
  const u32 chcr = c.chcr;
  const unsigned ts = (chcr >> kTsShift) & 3;
  const unsigned bytes = ts == 1 ? 2 : ts == 2 ? 4 : 1;  // TS = 11 is prohibited: taken as byte
  const unsigned rs = (chcr >> kRsShift) & 0xF;
  // Single address mode (RS = 2: memory -> device with DACK, RS = 3: device
  // -> memory).  The DACK side has no address on the real chip (9.3.4); it is
  // approximated as a dual transfer whose DACK-side address is still used
  // for the data but neither checked, counted nor charged.
  const bool src_is_dack = rs == 3, dst_is_dack = rs == 2;
  const bool single = src_is_dack || dst_is_dack;
  const u32 amask = ~u32(bytes - 1);

  bool err = false;
  if (!src_is_dack && address_error(c.sar, bytes)) err = true;
  if (!dst_is_dack && address_error(c.dar, bytes)) err = true;

  // DACK: always in single address mode, otherwise during the read (AM = 0)
  // or the write (AM = 1) cycle; AL selects the active level.
  const bool al = (chcr & kAl) != 0;
  const bool dack_on_read = single || !(chcr & kAm);
  const PinSink& dack = dack_[ch];

  if (dack && dack_on_read) dack(!al);
  u32 data;
  switch (bytes) {
    case 2: data = bus_.read16(c.sar & amask); break;
    case 4: data = bus_.read32(c.sar & amask); break;
    default: data = bus_.read8(c.sar); break;
  }
  if (dack) dack(dack_on_read ? al : !al);
  switch (bytes) {
    case 2: bus_.write16(c.dar & amask, u16(data)); break;
    case 4: bus_.write32(c.dar & amask, data); break;
    default: bus_.write8(c.dar, u8(data)); break;
  }
  if (dack && !dack_on_read) dack(al);

  // Bus time: two cycles in dual address mode, one in single address mode
  // (9.3.4), each as long as the BSC makes the accessed area (9.3.5).
  u32 states = 0;
  if (!src_is_dack) states += cost(c.sar, bytes);
  if (!dst_is_dack) states += cost(c.dar, bytes);
  cpu_.stall(states);

  // Address update (SM / DM: 00 fixed, 01 increment, 10 decrement, 11
  // prohibited -> fixed); ignored on the DACK side (9.2.4).
  const unsigned sm = (chcr >> kSmShift) & 3, dm = (chcr >> kDmShift) & 3;
  if (!src_is_dack) c.sar += sm == 1 ? bytes : sm == 2 ? u32(0) - bytes : 0;
  if (!dst_is_dack) c.dar += dm == 1 ? bytes : dm == 2 ? u32(0) - bytes : 0;
  c.tcr = (c.tcr - 1) & tcr_mask_;
  ++transfers_;

  // 9.3.6: the unit in progress completes and the registers are updated
  // before an address error halts every channel; TE is not set.
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

u32 Dmac::peek32(u32 addr) const {
  const unsigned ch = channel_of(addr);
  const Channel& c = ch_[ch];
  switch (addr & 0xC) {
    case kSar: return c.sar;
    case kDar: return c.dar;
    case kDmatcr: return c.tcr;  // bits 31-16 read 0 (9.2.3)
    default: return c.chcr & kChcrMask;
  }
}

u32 Dmac::read32(u32 addr) {
  if (addr < kChBase[0]) return (u32(read16(addr)) << 16) | read16(addr + 2);
  const u32 v = peek32(addr & ~3u);
  if ((addr & 0xC) == kChcr && (v & kTe)) ch_[channel_of(addr)].te_read = true;
  return v;
}

u16 Dmac::read16(u32 addr) {
  if (addr < kChBase[0]) {
    if ((addr & ~1u) != kDmaor) return 0xFFFF;  // empty addresses (usage note 10)
    dmaor_read_ |= u16(dmaor_ & (kAe | kNmif));
    return dmaor_;
  }
  const u32 v = peek32(addr & ~3u);
  if (addr & 2) {
    if ((addr & 0xC) == kChcr && (v & kTe)) ch_[channel_of(addr)].te_read = true;
    return u16(v);
  }
  return u16(v >> 16);
}

u8 Dmac::read8(u32 addr) {
  const u16 w = read16(addr & ~1u);
  return u8((addr & 1) ? w : w >> 8);
}

void Dmac::write32(u32 addr, u32 value) {
  if (addr < kChBase[0]) { write16(addr, u16(value >> 16)); write16(addr + 2, u16(value)); return; }
  write_reg(addr & ~3u, value, 0xFFFFFFFFu);
}

// 16-bit access: the half not accessed is held (table 9.2 note 2).
void Dmac::write16(u32 addr, u16 value) {
  if (addr < kChBase[0]) {
    if ((addr & ~1u) == kDmaor) write_dmaor(value);
    return;
  }
  const unsigned shift = (addr & 2) ? 0 : 16;
  write_reg(addr & ~3u, u32(value) << shift, 0xFFFFu << shift);
}

void Dmac::write8(u32 addr, u8 value) {
  if (addr < kChBase[0]) {
    if ((addr & ~1u) != kDmaor) return;
    const u16 w = (addr & 1) ? u16((dmaor_ & 0xFF00) | value) : u16((dmaor_ & 0x00FF) | (u16(value) << 8));
    write_dmaor(w);
    return;
  }
  const unsigned shift = 8 * (3 - (addr & 3));
  write_reg(addr & ~3u, u32(value) << shift, 0xFFu << shift);
}

void Dmac::write_reg(u32 addr, u32 value, u32 mask) {
  const unsigned ch = channel_of(addr);
  Channel& c = ch_[ch];
  switch (addr & 0xC) {
    case kSar: c.sar = (c.sar & ~mask) | (value & mask); break;
    case kDar: c.dar = (c.dar & ~mask) | (value & mask); break;
    case kDmatcr: c.tcr = ((c.tcr & ~mask) | (value & mask)) & tcr_mask_; break;  // bits above the counter read 0
    default: write_chcr(ch, value, mask); return;
  }
}

void Dmac::write_chcr(unsigned ch, u32 value, u32 mask) {
  Channel& c = ch_[ch];
  u32 nv = ((c.chcr & ~mask) | (value & mask)) & kChcrMask & ~kTe;
  // TE: read/(write): only a 0 written after reading 1 clears it (9.2.4).
  bool te = (c.chcr & kTe) != 0;
  if (mask & kTe) {
    if (te && !(value & kTe) && c.te_read) te = false;
    c.te_read = false;
  }
  c.chcr = nv | (te ? kTe : 0);
  update_irq(ch);
  kick();
}

// DMAOR: DME read/write; AE and NMIF clear on a 0 written after reading 1
// and cannot be set by software (9.2.5).  Clearing a flag resumes the
// channels that are still enabled (9.3.6).
void Dmac::write_dmaor(u16 value) {
  u16 flags = u16(dmaor_ & (kAe | kNmif));
  flags &= u16(~(dmaor_read_ & ~value & (kAe | kNmif)));
  dmaor_read_ = 0;
  dmaor_ = u16((value & kDme) | flags);
  kick();
}

}  // namespace sh2
