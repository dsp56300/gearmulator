#include "cpu/h8300h/periph2320.hpp"

#include <bit>

#include <algorithm>
#include <cstdio>

namespace h8300h {

namespace {
// the system registers (the H8/3002's of the same names, types.hpp, sit elsewhere)
namespace reg {
constexpr u32 kIpra = 0xFFFEC4, kAbwcr = 0xFFFED0, kWcrl = 0xFFFED3, kSbycr = 0xFFFF38, kSyscr = 0xFFFF39;
}  // namespace reg
constexpr u32 kDmaAddress = 0xFFFEE0, kDmaControl = 0xFFFF00;
constexpr u32 kTstr = 0xFFFFC0, kTsyr = 0xFFFFC1;
constexpr u32 kTpuBase[6] = {0xFFFFD0, 0xFFFFE0, 0xFFFFF0, 0xFFFE80, 0xFFFE90, 0xFFFEA0};  // TPU3-5 sit lower
constexpr u32 kSciBase = 0xFFFF78, kAdcBase = 0xFFFF90;
constexpr u32 kPortDdr = 0xFFFEB0, kPortPins = 0xFFFF50, kPortData = 0xFFFF60;

// SCI SCR / SSR bits
constexpr u8 kTie = 0x80, kRie = 0x40, kTe = 0x20, kRe = 0x10, kTeie = 0x04;
constexpr u8 kTdre = 0x80, kRdrf = 0x40, kOrer = 0x20, kTend = 0x04;

// A/D: a first conversion takes the longest table 16.4 allows (its start depends on where the ADCSR write falls
// against the converter's clock, which is not modelled); the ones after it in scan mode the time of table 16.5.
constexpr u32 kAdcFirstStates[4] = {530, 68, 266, 134};
constexpr u32 kAdcScanStates[4] = {512, 64, 256, 128};

// Table 5.4: the 3-bit priority field in IPRA-IPRK behind each vector (field 0 is IPRA's upper half), -1 for none.
int ipr_field(unsigned vector) {
  static constexpr struct { u8 last; s8 field; } groups[] = {
      {15, -1}, {16, 0}, {17, 1}, {19, 2}, {21, 3}, {23, 4}, {24, 5}, {25, 6}, {26, 7}, {27, 8}, {28, 9}, {31, -1},
      {39, 10}, {43, 11}, {47, 12}, {55, 13}, {59, 14}, {63, 15}, {67, 16}, {71, 17}, {79, 18}, {83, 19}, {87, 20},
      {91, 21}};
  for (const auto& g : groups)
    if (vector <= g.last) return g.field;
  return -1;
}
}  // namespace

Peripherals2320::Peripherals2320(emu::Scheduler& sched, Cpu& cpu, Bus& bus, emu::IoMux& io)
    : sched_(sched), cpu_(cpu), bus_(bus), io_(io) {
  io_.set_fallback(&file_dev_);
  io_.assign(reg::kIpra, u32(ipr_.size()), &sys_dev_);
  io_.assign(reg::kAbwcr, 4, &sys_dev_);  // ABWCR, ASTCR, WCRH, WCRL
  io_.assign(reg::kSbycr, 2, &sys_dev_);  // SBYCR, SYSCR
  io_.assign(kDmaAddress, 32, &dma_dev_);
  io_.assign(kDmaControl, 8, &dma_dev_);
  io_.assign(kTstr, 2, &tpu_dev_);
  for (const u32 base : kTpuBase) io_.assign(base, 16, &tpu_dev_);
  io_.assign(kSciBase, 8 * kSciCount, &sci_dev_);
  io_.assign(kAdcBase, 10, &adc_dev_);
  io_.assign(kPortDdr, kPortCount, &port_dev_);
  io_.assign(kPortPins, kPortCount, &port_dev_);
  io_.assign(kPortData, kPortCount, &port_dev_);
  for (unsigned i = 0; i < tpu_.size(); ++i) { tpu_[i].owner = this; tpu_[i].index = i; }
  for (unsigned i = 0; i < kSciCount; ++i) { sci_[i].owner = this; sci_[i].index = i; }
  cpu_.set_intc(this);
}

Peripherals2320::~Peripherals2320() {
  cpu_.set_intc(nullptr);
  for (auto& c : tpu_) cancel(c.event);
  for (auto& s : sci_) { cancel(s.tx_event); cancel(s.rx_event); }
  cancel(adc_event_);
}

void Peripherals2320::reset() {
  pending_ = {};
  level_ = {};
  file_.fill(0);
  ipr_.fill(0x77);
  syscr_ = 0x01;
  sbycr_ = 0x08;
  // bus controller (section 6): 16-bit areas (mode 4), three states, three waits
  file_[reg::kAbwcr - kRegBase] = 0x00;
  file_[reg::kAbwcr + 1 - kRegBase] = 0xFF;
  file_[reg::kAbwcr + 2 - kRegBase] = 0xFF;
  file_[reg::kAbwcr + 3 - kRegBase] = 0xFF;
  apply_bus_controller();
  // DMAC
  dma_ = {};
  dmacr_ = {};
  dmawer_ = dmatcr_ = dmabcrh_ = dmabcrl_ = 0;
  dma_ended_ = {};
  // TPU
  tpu_reset();
  // SCI
  for (unsigned ch = 0; ch < kSciCount; ++ch) sci_reset(ch);
  // A/D
  cancel(adc_event_);
  addr_ = {};
  adcsr_ = 0;
  adcr_ = 0x3F;
  adc_temp_ = 0;
  // ports
  pddr_ = {};
  pdr_ = {};
  update_irq();
}

void Peripherals2320::enter_software_standby() {
  // the modules that reset in software standby; the ports keep their state
  tpu_reset();
  for (unsigned ch = 0; ch < kSciCount; ++ch) sci_reset(ch);
  cancel(adc_event_);
  adcsr_ = 0;
  adc_update_level();
  update_irq();
}

// ---------------------------------------------------------------------------
// Interrupt controller (section 5)

u8 Peripherals2320::level(unsigned which) const {
  const int field = ipr_field(which);
  if (field < 0) return 7;  // NMI, which is never masked; nothing else without a field is ever requested
  const u8 ipr = ipr_[unsigned(field) >> 1];
  return (field & 1) ? u8(ipr & 7) : u8((ipr >> 4) & 7);
}

// Mode 2 (5.4.3): the pending source with the highest priority wins, ties go to the lowest vector, and it is only
// accepted above the mask level in EXR.  Mode 0: the I bit masks everything but NMI, the lowest vector first.
bool Peripherals2320::arbitrate(unsigned& which, u8 mask) const {
  const u64 r0 = pending_[0] | level_[0], r1 = pending_[1] | level_[1];
  if (!(r0 | r1)) return false;
  if ((r0 >> kNmi) & 1) { which = kNmi; return true; }
  if (control_mode() != 2) {
    if (mask & kCcrI) return false;
    which = r0 ? unsigned(std::countr_zero(r0)) : 64u + unsigned(std::countr_zero(r1));
    return true;
  }
  const unsigned floor = mask & kExrMask;
  int best = -1;
  unsigned best_level = floor;
  for (unsigned w = 0; w < 2; ++w) {
    u64 req = w ? r1 : r0;
    while (req) {
      const unsigned v = w * 64 + unsigned(std::countr_zero(req));
      req &= req - 1;
      const unsigned l = level(v);
      if (l > best_level) { best = int(v); best_level = l; }  // counting up: ties keep the lower vector
    }
  }
  if (best < 0) return false;
  which = unsigned(best);
  return true;
}

void Peripherals2320::acknowledge(unsigned which) {
  pending_[which >> 6] &= ~(u64(1) << (which & 63));
  last_accepted_ = which;
}

bool Peripherals2320::standby_wake(u8) const { return bit(pending_, kNmi) || bit(level_, kNmi); }

void Peripherals2320::request(unsigned which) {
  if (which >= kVectors) return;
  pending_[which >> 6] |= u64(1) << (which & 63);
  update_irq();
}

void Peripherals2320::set_level(unsigned which, bool asserted) {
  if (which >= kVectors) return;
  u64& word = level_[which >> 6];
  const u64 b = u64(1) << (which & 63);
  if (((word & b) != 0) == asserted) return;
  word = asserted ? (word | b) : (word & ~b);
  update_irq();
}

bool Peripherals2320::requested(unsigned which) const {
  return which < kVectors && (bit(pending_, which) || bit(level_, which));
}

// ---------------------------------------------------------------------------
// System and bus controller registers

u8 Peripherals2320::sys_read(u32 a) {
  if (a >= reg::kIpra && a < reg::kIpra + ipr_.size()) return ipr_[a - reg::kIpra];
  if (a == reg::kSbycr) return sbycr_;
  if (a == reg::kSyscr) return syscr_;
  return file_[a - kRegBase];
}

void Peripherals2320::sys_write(u32 a, u8 v) {
  if (a >= reg::kIpra && a < reg::kIpra + ipr_.size()) { ipr_[a - reg::kIpra] = u8(v & 0x77); update_irq(); return; }
  if (a == reg::kSbycr) { sbycr_ = v; return; }
  if (a == reg::kSyscr) { syscr_ = v; update_irq(); return; }  // INTM switches the interrupt control mode
  file_[a - kRegBase] = v;
  if (a >= reg::kAbwcr && a <= reg::kWcrl) apply_bus_controller();
}

// Section 6: an area is 8 or 16 bits wide (ABWCR), takes two or three states (ASTCR), and a three-state one adds the
// waits of its two bits in WCRH (areas 7-4) / WCRL (areas 3-0).  The on-chip RAM takes one state per word, the
// supporting modules two.
void Peripherals2320::apply_bus_controller() {
  const u8 abwcr = file_[reg::kAbwcr - kRegBase], astcr = file_[reg::kAbwcr + 1 - kRegBase];
  const u16 wcr = u16((file_[reg::kAbwcr + 2 - kRegBase] << 8) | file_[reg::kAbwcr + 3 - kRegBase]);
  for (unsigned area = 0; area < 8; ++area) {
    emu::AccessClass k;
    k.bus_bytes = ((abwcr >> area) & 1) ? 1 : 2;
    k.states = ((astcr >> area) & 1) ? 3 : 2;
    k.waits = k.states == 3 ? u8((wcr >> (area * 2)) & 3) : 0;
    k.external = true;
    bus_.set_class(kClsArea0 + area, k);
  }
  emu::AccessClass ram;
  ram.bus_bytes = 2; ram.states = 1; ram.waits = 0; ram.external = false;
  bus_.set_class(kClsRam, ram);
  emu::AccessClass regs;
  regs.bus_bytes = 2; regs.states = 2; regs.waits = 0; regs.external = false;
  bus_.set_class(kClsRegisters, regs);
  emu::AccessClass unmapped;
  unmapped.bus_bytes = 2; unmapped.states = 3; unmapped.waits = 0; unmapped.external = true;
  bus_.set_class(Bus::kClsUnmapped, unmapped);
}

// ---------------------------------------------------------------------------
// DMA controller (section 7), short address mode only: four channels 0A, 0B,
// 1A and 1B, each moving between a 24-bit address in MAR and a 16-bit one in
// IOAR in the H'FFxxxx page.  Sequential, idle and repeat modes.  Full address
// mode (FAE) and single address mode (SAE) pair the channels and drive DACK;
// no firmware here uses them, so a channel set up that way is left alone and
// reported once.

u8 Peripherals2320::dma_read(u32 a) {
  if (a >= kDmaControl) {
    switch (a - kDmaControl) {
      case 0: return dmawer_;
      case 1: return dmatcr_;
      case 6: return dmabcrh_;
      case 7: return dmabcrl_;
      default: return dmacr_[a - kDmaControl - 2];
    }
  }
  const DmaChannel& c = dma_[(a - kDmaAddress) >> 3];
  switch (a & 7) {
    case 0: return 0;  // the top byte of MAR is reserved
    case 1: return u8(c.mar >> 16);
    case 2: return u8(c.mar >> 8);
    case 3: return u8(c.mar);
    case 4: return u8(c.ioar >> 8);
    case 5: return u8(c.ioar);
    case 6: return u8(c.etcr >> 8);
    default: return u8(c.etcr);
  }
}

void Peripherals2320::dma_write(u32 a, u8 v) {
  if (a >= kDmaControl) {
    switch (a - kDmaControl) {
      case 0: dmawer_ = v; return;
      case 1: dmatcr_ = v; return;
      case 6: dmabcrh_ = v; break;
      case 7: dmabcrl_ = v; dma_update_end(); break;
      default: dmacr_[a - kDmaControl - 2] = v; break;
    }
    dma_reoffer();
    return;
  }
  DmaChannel& c = dma_[(a - kDmaAddress) >> 3];
  switch (a & 7) {
    case 0: return;
    case 1: c.mar = (c.mar & 0x00FFFF) | (u32(v) << 16); return;
    case 2: c.mar = (c.mar & 0xFF00FF) | (u32(v) << 8); return;
    case 3: c.mar = (c.mar & 0xFFFF00) | v; return;
    case 4: c.ioar = u16((v << 8) | (c.ioar & 0xFF)); return;
    case 5: c.ioar = u16((c.ioar & 0xFF00) | v); return;
    case 6: c.etcr = u16((v << 8) | (c.etcr & 0xFF)); return;
    default: c.etcr = u16((c.etcr & 0xFF00) | v); return;
  }
}

bool Peripherals2320::dma_unsupported(unsigned ch) {
  const unsigned pair = ch >> 1;
  if (!(dmabcrh_ & (0x40u << pair)) && !(dmabcrh_ & (0x10u << pair))) return false;
  if (!dma_reported_[ch]) {
    dma_reported_[ch] = true;
    std::printf("DMAC channel %u%c asks for full or single address mode, which is not implemented\n", pair,
                (ch & 1) ? 'B' : 'A');
  }
  return true;
}

bool Peripherals2320::dma_activate(DmaSource source) {
  bool acknowledged = false, transferred = false;
  for (unsigned ch = 0; ch < dma_.size(); ++ch) {
    if (!dma_dte(ch) || (dmacr_[ch] & 0x0F) != source || dma_unsupported(ch)) continue;
    dma_transfer(ch);
    transferred = true;
    if (dmabcrh_ & (1u << ch)) acknowledged = true;  // DTA: the DMAC clears the source's flag
  }
  if (transferred) dma_update_end();
  return acknowledged;
}

void Peripherals2320::dma_transfer(unsigned ch) {
  DmaChannel& c = dma_[ch];
  const u8 cr = dmacr_[ch];
  const unsigned size = (cr & 0x80) ? 2 : 1;            // DTSZ
  const int step = (cr & 0x40) ? -int(size) : int(size);  // DTID
  const bool repeat_or_idle = (cr & 0x20) != 0;         // RPE
  const bool io_is_source = (cr & 0x10) != 0;           // DTDIR
  const bool idle = repeat_or_idle && ((dmabcrl_ >> ch) & 1);  // RPE with DTIE
  const u32 io = 0xFF0000u | c.ioar, memory = c.mar & kAddrMask;
  const u32 from = io_is_source ? io : memory, to = io_is_source ? memory : io;

  // the transfer takes the bus: the CPU waits for it
  cpu_.stall(cpu_.access_states(from, size) + cpu_.access_states(to, size));
  const u16 value = size == 2 ? u16((dma_read8(from) << 8) | dma_read8(from + 1)) : dma_read8(from);

  // The channel moves on before the write: a write that frees its destination (TDR) asks for the next transfer at
  // once, and that one has to find the next address and the count that is left.
  if (idle) {  // both addresses stay put, ETCR counts down to the end
    if (!--c.etcr) dmabcrl_ &= u8(~(0x10u << ch));
  } else if (!repeat_or_idle) {  // sequential
    c.mar = u32(s32(c.mar) + step) & kAddrMask;
    if (!--c.etcr) dmabcrl_ &= u8(~(0x10u << ch));
  } else {
    // repeat: ETCRL counts, ETCRH holds the length and reloads it, and MAR goes back to where it started
    c.mar = u32(s32(c.mar) + step) & kAddrMask;
    u8 counter = u8(c.etcr - 1);
    if (!counter) {
      const u8 length = u8(c.etcr >> 8);
      counter = length;
      c.mar = u32(s32(c.mar) - step * s32(length)) & kAddrMask;
    }
    c.etcr = u16((c.etcr & 0xFF00) | counter);
  }
  // clearing DTE ends a transfer, and dma_update_end() turns that into the interrupt if DTIE says so

  if (size == 2) {
    dma_write8(to, u8(value >> 8));
    dma_write8(to + 1, u8(value));
  } else {
    dma_write8(to, u8(value));
  }
}

// DTIE set while DTE is clear is what the manual calls the end of a transfer.  The request is raised once when that
// becomes true, not for as long as it holds: firmware here leaves DTIE set, points the vector at a bare RTE and
// restarts the transfer from its main loop, which is what its DMABCRL polling is.
void Peripherals2320::dma_update_end() {
  bool changed = false;
  for (unsigned ch = 0; ch < dma_.size(); ++ch) {
    const bool ended = !dma_dte(ch) && ((dmabcrl_ >> ch) & 1);
    if (ended == dma_ended_[ch]) continue;
    dma_ended_[ch] = ended;
    const unsigned v = 72 + ch;  // DEND0A, DEND0B, DEND1A, DEND1B
    if (ended) pending_[v >> 6] |= u64(1) << (v & 63);
    else pending_[v >> 6] &= ~(u64(1) << (v & 63));
    changed = true;
  }
  if (changed) update_irq();
}

// The SCI transmitters are level sources: a channel enabled while TDRE is already set starts at once.
void Peripherals2320::dma_reoffer() {
  for (unsigned ch = 0; ch < 2; ++ch) {
    sci_offer_dma(ch);
    sci_update_levels(ch);
  }
}

u8 Peripherals2320::dma_read8(u32 a) {
  a &= kAddrMask;
  const int ch = sci_channel_of(a);
  if (ch >= 0 && (a & 7) == 5) return sci_dma_read_rdr(unsigned(ch));
  return bus_.read8(a);
}

void Peripherals2320::dma_write8(u32 a, u8 v) {
  a &= kAddrMask;
  const int ch = sci_channel_of(a);
  if (ch >= 0 && (a & 7) == 3) { sci_dma_write_tdr(unsigned(ch), v); return; }
  bus_.write8(a, v);
}

// ---------------------------------------------------------------------------
// 16-bit timer pulse unit (section 10): six channels with their own prescaler
// and two or four general registers.  Internal clocks only: a channel asked
// for an external clock or another channel's overflow does not count.  The
// flag of a compare match goes up on the clock at which the counter holds the
// register's value, and with counter clearing the next value is zero.

void Peripherals2320::tpu_reset() {
  for (auto& c : tpu_) {
    cancel(c.event);
    Peripherals2320* owner = c.owner;
    const unsigned index = c.index;
    c = TpuChannel{};
    c.owner = owner;
    c.index = index;
    c.synced = now();
  }
  tstr_ = tsyr_ = 0;
  for (unsigned ch = 0; ch < tpu_.size(); ++ch) tpu_update_levels(ch);
}

int Peripherals2320::tpu_channel_of(u32 a, u32& offset) {
  for (unsigned ch = 0; ch < 6; ++ch) {
    if (a >= kTpuBase[ch] && a < kTpuBase[ch] + 16) {
      offset = a - kTpuBase[ch];
      return int(ch);
    }
  }
  return -1;
}

// The counter clock as a shift of the state count, or -1 for a clock that is not driven: TPSC 0-3 are phi/1, /4,
// /16, /64 everywhere, 4-7 differ per channel (tables 10.6-10.11).  Counting both edges halves the period.
int Peripherals2320::tpu_shift(unsigned ch) const {
  const unsigned tpsc = tpu_[ch].tcr & 7;
  int shift;
  if (tpsc < 4) {
    shift = int(tpsc * 2);
  } else {
    switch (ch * 8 + tpsc) {
      case 1 * 8 + 6: shift = 8; break;   // phi/256
      case 2 * 8 + 7: shift = 10; break;  // phi/1024
      case 3 * 8 + 5: shift = 10; break;  // phi/1024
      case 3 * 8 + 6: shift = 8; break;   // phi/256
      case 3 * 8 + 7: shift = 12; break;  // phi/4096
      case 4 * 8 + 6: shift = 10; break;  // phi/1024
      case 5 * 8 + 6: shift = 8; break;   // phi/256
      default: return -1;                 // an external clock or another channel's overflow
    }
  }
  if (shift >= 2 && (tpu_[ch].tcr & 0x10)) --shift;
  return shift;
}

// CCLR in TCR: 1 selects TGRA, 2 TGRB, and on channels 0 and 3 a set CCLR2 moves that to TGRC and TGRD.
bool Peripherals2320::tpu_cleared_by(unsigned ch, unsigned tgr) const {
  const unsigned cclr = (tpu_[ch].tcr >> 5) & (tpu_tgr_count(ch) == 4 ? 7u : 3u);
  if (cclr == 1 || cclr == 2) return tgr == cclr - 1;
  if (cclr == 5 || cclr == 6) return tgr == cclr - 3;
  return false;
}

// The clock, counted from the next one as 1, at which something happens: a compare match or the overflow.
u32 Peripherals2320::tpu_clocks_to_event(unsigned ch) const {
  const TpuChannel& c = tpu_[ch];
  u32 d = 0x10000u - c.tcnt;
  for (unsigned g = 0; g < tpu_tgr_count(ch); ++g) d = std::min(d, ((u32(c.tgr[g]) - c.tcnt) & 0xFFFF) + 1);
  return d;
}

void Peripherals2320::tpu_clock(unsigned ch) {
  TpuChannel& c = tpu_[ch];
  u16 next = u16(c.tcnt + 1);
  if (!next) tpu_raise(ch, 4);  // TCFV
  for (unsigned g = 0; g < tpu_tgr_count(ch); ++g) {
    if (c.tcnt != c.tgr[g]) continue;
    tpu_raise(ch, g);
    if (tpu_cleared_by(ch, g)) next = 0;
  }
  c.tcnt = next;
}

void Peripherals2320::tpu_raise(unsigned ch, unsigned flag) {
  TpuChannel& c = tpu_[ch];
  c.tsr |= u8(1u << flag);
  // a TGRA compare match activates the DMAC, and a channel taking it with DTA set clears the flag
  if (flag == 0 && dma_activate(DmaSource(kDmaTpu0A + ch))) c.tsr &= u8(~1u);
  tpu_update_levels(ch);
}

// TGIA, TGIB, [TGIC, TGID,] TCIV of each channel, in this order from its base (table 5.4)
void Peripherals2320::tpu_update_levels(unsigned ch) {
  static constexpr unsigned bases[6] = {32, 40, 44, 48, 56, 60};
  const TpuChannel& c = tpu_[ch];
  const u8 active = u8(c.tsr & c.tier);
  const unsigned n = tpu_tgr_count(ch);
  for (unsigned g = 0; g < n; ++g) set_level(bases[ch] + g, (active >> g) & 1);
  set_level(bases[ch] + n, (active >> 4) & 1);
}

void Peripherals2320::tpu_sync(unsigned ch, u64 t) {
  TpuChannel& c = tpu_[ch];
  if (t < c.synced) c.synced = t;
  if (!tpu_running(ch)) { c.synced = t; return; }
  const unsigned shift = unsigned(tpu_shift(ch));
  u64 clocks = (t >> shift) - (c.synced >> shift);
  c.synced = t;
  while (clocks) {
    const u32 d = tpu_clocks_to_event(ch);
    if (d > clocks) { c.tcnt = u16(c.tcnt + clocks); break; }
    c.tcnt = u16(c.tcnt + d - 1);
    tpu_clock(ch);
    clocks -= d;
  }
}

void Peripherals2320::tpu_reschedule(unsigned ch) {
  TpuChannel& c = tpu_[ch];
  cancel(c.event);
  if (!tpu_running(ch)) return;
  const unsigned shift = unsigned(tpu_shift(ch));
  const u64 when = ((c.synced >> shift) + tpu_clocks_to_event(ch)) << shift;
  c.event = sched_.schedule(when, &on_tpu, &c);
}

void Peripherals2320::on_tpu(void* channel, u64, u64) {
  auto* c = static_cast<TpuChannel*>(channel);
  Peripherals2320* p = c->owner;
  c->event = 0;
  p->tpu_sync(c->index, p->now());
  p->tpu_reschedule(c->index);
}

u8 Peripherals2320::tpu_read(u32 a) {
  if (a == kTstr) return tstr_;
  if (a == kTsyr) return tsyr_;
  u32 o = 0;
  const int ch = tpu_channel_of(a, o);
  if (ch < 0) return 0;
  TpuChannel& c = tpu_[unsigned(ch)];
  switch (o) {
    case 0: return c.tcr;
    case 1: return c.tmdr;
    case 2: return c.tiorh;
    case 3: return c.tiorl;
    case 4: return c.tier;
    case 5: return c.tsr;
    case 6: tpu_sync(unsigned(ch), now()); return u8(c.tcnt >> 8);
    case 7: tpu_sync(unsigned(ch), now()); return u8(c.tcnt);
    default: {
      const u16 g = c.tgr[(o - 8) >> 1];
      return (o & 1) ? u8(g) : u8(g >> 8);
    }
  }
}

void Peripherals2320::tpu_write(u32 a, u8 v) {
  if (a == kTstr || a == kTsyr) {
    tpu_sync_all(now());
    (a == kTstr ? tstr_ : tsyr_) = v;
    for (unsigned ch = 0; ch < tpu_.size(); ++ch) tpu_reschedule(ch);
    return;
  }
  u32 o = 0;
  const int i = tpu_channel_of(a, o);
  if (i < 0) return;
  const unsigned ch = unsigned(i);
  TpuChannel& c = tpu_[ch];
  tpu_sync(ch, now());
  switch (o) {
    case 0: c.tcr = v; break;
    case 1: c.tmdr = v; break;
    case 2: c.tiorh = v; break;
    case 3: c.tiorl = v; break;
    case 4: c.tier = v; tpu_update_levels(ch); break;
    case 5: c.tsr &= v; tpu_update_levels(ch); break;  // a flag is cleared by writing 0 to it
    case 6: c.tcnt = u16((v << 8) | (c.tcnt & 0xFF)); break;
    case 7: c.tcnt = u16((c.tcnt & 0xFF00) | v); break;
    default: {
      u16& g = c.tgr[(o - 8) >> 1];
      g = (o & 1) ? u16((g & 0xFF00) | v) : u16((v << 8) | (g & 0xFF));
      break;
    }
  }
  tpu_reschedule(ch);
}

// ---------------------------------------------------------------------------
// Serial communication interface (section 14), at the bit rate BRR asks for.
// In clocked synchronous mode every byte shifted out shifts the peer's answer
// in.  A character is counted as ten bit times in asynchronous mode, which is
// right for 8N1; the smart card interface is not here.

void Peripherals2320::sci_reset(unsigned ch) {
  Sci& s = sci_[ch];
  cancel(s.tx_event);
  cancel(s.rx_event);
  s.smr = 0; s.brr = 0xFF; s.scr = 0; s.tdr = 0xFF; s.ssr = 0x84; s.rdr = 0; s.scmr = 0xF2;
  s.shifting = false;
  s.rx_next = 0;
  s.queue.clear();
  sci_update_levels(ch);
}

// One bit is 8 * 2^(2n-1) * (BRR+1) states in synchronous mode and eight times that in asynchronous (14.2.8).
u64 Peripherals2320::sci_char_states(const Sci& s) const {
  const bool synchronous = (s.smr & 0x80) != 0;
  const u64 bit = (u64(synchronous ? 4 : 32) * (u64(s.brr) + 1)) << (2 * (s.smr & 3));
  return bit * (synchronous ? 8 : 10);
}

u8 Peripherals2320::sci_read(u32 a) {
  const unsigned ch = unsigned(sci_channel_of(a));
  Sci& s = sci_[ch];
  switch (a & 7) {
    case 0: return s.smr;
    case 1: return s.brr;
    case 2: return s.scr;
    case 3: return s.tdr;
    case 4: return s.ssr;
    case 5: return sci_dma_read_rdr(ch);  // reading the data takes RDRF down with it
    case 6: return s.scmr;
    default: return 0xFF;
  }
}

void Peripherals2320::sci_write(u32 a, u8 v) {
  const unsigned ch = unsigned(sci_channel_of(a));
  Sci& s = sci_[ch];
  switch (a & 7) {
    case 0: s.smr = v; return;
    case 1: s.brr = v; return;
    case 2:
      s.scr = v;
      sci_transmitter_off(ch);
      sci_schedule_rx(ch);
      sci_kick(ch, now());
      return;
    case 3:
      // Only a DMAC write clears TDRE by itself.  The CPU writes TDR and then clears TDRE by hand, and that second
      // write is what hands the byte to the transmitter (14.3.5).
      s.tdr = v;
      return;
    case 4: {
      // only a 0 clears a flag; TEND is read only but goes down with TDRE
      const bool clears_tdre = (s.ssr & kTdre) && !(v & kTdre);
      s.ssr &= u8(v | kTend);
      if (clears_tdre) s.ssr &= u8(~kTend);
      sci_transmitter_off(ch);
      sci_kick(ch, now());
      return;
    }
    case 6: s.scmr = v; return;
    default: return;
  }
}

void Peripherals2320::sci_dma_write_tdr(unsigned ch, u8 v) {
  Sci& s = sci_[ch];
  s.tdr = v;
  s.ssr &= u8(~(kTdre | kTend));
  sci_kick(ch, now());
}

u8 Peripherals2320::sci_dma_read_rdr(unsigned ch) {
  Sci& s = sci_[ch];
  s.ssr &= u8(~kRdrf);
  sci_update_levels(ch);
  return s.rdr;
}

// TDRE and TEND are held at 1 while TE is 0, and clearing TE initialises the shift register (14.3.5): without that,
// a firmware that clears TDRE before it enables the transmitter sends what was left in TDR as an extra first byte.
void Peripherals2320::sci_transmitter_off(unsigned ch) {
  Sci& s = sci_[ch];
  if (s.scr & kTe) return;
  s.ssr |= kTdre | kTend;
  s.shifting = false;
  cancel(s.tx_event);
}

// TDRE clear while the shift register is free hands TDR to it, which frees TDR again.  `at` is when the shift
// starts: now for a byte the firmware starts, the previous one's end for one that follows on.
void Peripherals2320::sci_kick(unsigned ch, u64 at) {
  Sci& s = sci_[ch];
  if (!s.shifting && (s.scr & kTe) && !(s.ssr & kTdre)) {
    s.shift_byte = s.tdr;
    s.shifting = true;
    s.ssr = u8((s.ssr | kTdre) & ~kTend);
    s.tx_event = sched_.schedule(at + sci_char_states(s), &on_sci_tx, &s);
  }
  sci_offer_dma(ch);
  sci_update_levels(ch);
}

void Peripherals2320::sci_offer_dma(unsigned ch) {
  const Sci& s = sci_[ch];
  if (ch > 1 || !(s.scr & kTe) || !(s.ssr & kTdre)) return;
  dma_activate(ch ? kDmaSci1Txi : kDmaSci0Txi);
}

void Peripherals2320::sci_update_levels(unsigned ch) {
  const Sci& s = sci_[ch];
  const unsigned base = 80 + 4 * ch;
  set_level(base + 0, (s.scr & kRie) && (s.ssr & 0x38));                      // ERI: ORER, FER, PER
  set_level(base + 1, (s.scr & kRie) && (s.ssr & kRdrf));                     // RXI
  set_level(base + 2, (s.scr & (kTie | kTe)) == (kTie | kTe) && (s.ssr & kTdre));  // TXI
  set_level(base + 3, (s.scr & kTeie) && (s.ssr & kTend));                    // TEI
}

void Peripherals2320::sci_receive_byte(unsigned ch, u8 byte) {
  Sci& s = sci_[ch];
  if (!(s.scr & kRe)) return;
  if (s.ssr & kRdrf) s.ssr |= kOrer;  // the CPU did not take the last one
  else { s.rdr = byte; s.ssr |= kRdrf; }
  sci_update_levels(ch);
}

void Peripherals2320::receive(unsigned ch, u8 byte) {
  if (ch >= kSciCount) return;
  sci_[ch].queue.push_back(byte);
  sci_schedule_rx(ch);
}

void Peripherals2320::sci_schedule_rx(unsigned ch) {
  Sci& s = sci_[ch];
  if (s.rx_event || s.queue.empty() || !(s.scr & kRe)) return;
  s.rx_event = sched_.schedule(std::max(now(), s.rx_next), &on_sci_rx, &s);
}

void Peripherals2320::on_sci_tx(void* sci, u64 when, u64) {
  auto* s = static_cast<Sci*>(sci);
  Peripherals2320& p = *s->owner;
  const unsigned ch = s->index;
  s->tx_event = 0;
  s->shifting = false;
  if (s->tx_hook) s->tx_hook(s->shift_byte);
  ++s->transmitted;
  if (s->peer) p.sci_receive_byte(ch, s->peer(s->shift_byte));
  if (s->ssr & kTdre) s->ssr |= kTend;  // nothing waiting in TDR: the line has gone idle
  p.sci_kick(ch, when);
}

void Peripherals2320::on_sci_rx(void* sci, u64 when, u64) {
  auto* s = static_cast<Sci*>(sci);
  Peripherals2320& p = *s->owner;
  const unsigned ch = s->index;
  s->rx_event = 0;
  if (!(s->scr & kRe) || s->queue.empty()) return;
  const u8 byte = s->queue.front();
  s->queue.pop_front();
  s->rx_next = when + p.sci_char_states(*s);
  p.sci_receive_byte(ch, byte);
  p.sci_schedule_rx(ch);
}

// ---------------------------------------------------------------------------
// A/D converter (section 16), the eight-channel one of the 2320: ten bits,
// single and scan mode, the ADI interrupt and the TEMP register behind the
// lower byte of every ADDR.  Conversions start by software only.

void Peripherals2320::set_analog(unsigned ch, u16 value) {
  if (ch < kAnalogInputs) adc_input_[ch] = std::min<u16>(value, 1023);
}

u8 Peripherals2320::adc_read(u32 a) {
  const u32 o = a - kAdcBase;
  if (o == 8) return adcsr_;
  if (o == 9) return u8(adcr_ | 0x33);  // bits 5, 4, 1 and 0 always read 1
  // The upper byte is read directly and hands the lower one to TEMP, which a read of the lower byte returns (16.3).
  if (o & 1) return adc_temp_;
  const u16 v = addr_[o >> 1];
  adc_temp_ = u8(v);
  return u8(v >> 8);
}

void Peripherals2320::adc_write(u32 a, u8 v) {
  const u32 o = a - kAdcBase;
  if (o == 9) { adcr_ = v; return; }
  if (o != 8) return;  // ADDR is read only
  const bool was_running = (adcsr_ & 0x20) != 0;
  adcsr_ = u8((v & ~0x80) | (adcsr_ & v & 0x80));  // software can clear ADF by writing 0, never set it
  if (!(adcsr_ & 0x20)) cancel(adc_event_);  // the conversion under way is lost
  else if (!was_running) adc_convert(adc_first(), now() + kAdcFirstStates[adc_clock_select()]);
  adc_update_level();
}

// the input is sampled when a channel's conversion starts and lands in ADDR when it ends
void Peripherals2320::adc_convert(unsigned ch, u64 end) {
  adc_channel_ = ch;
  adc_sample_ = adc_input_[ch & 7];
  adc_end_ = end;
  cancel(adc_event_);
  adc_event_ = sched_.schedule(end, &on_adc, this);
}

void Peripherals2320::on_adc(void* self, u64, u64) {
  auto* p = static_cast<Peripherals2320*>(self);
  p->adc_event_ = 0;
  p->adc_finish();
}

void Peripherals2320::adc_finish() {
  addr_[adc_channel_ & 3] = u16(adc_sample_ << 6);  // the ten bits sit at the top
  if (!(adcsr_ & 0x10)) {
    adcsr_ = u8((adcsr_ | 0x80) & ~0x20);
    adc_update_level();
    return;
  }
  // Scan mode goes round the group until ADST is cleared, setting ADF after its last channel; at or past it, so a
  // group changed mid scan (which the manual forbids) cannot run off the end.
  unsigned next = adc_channel_ + 1;
  if (adc_channel_ >= adc_first() + (adcsr_ & 3u)) {
    adcsr_ |= 0x80;
    next = adc_first();
  }
  adc_update_level();
  adc_convert(next, adc_end_ + kAdcScanStates[adc_clock_select()]);
}

// ---------------------------------------------------------------------------
// I/O ports (section 9): data direction, data and pin registers of ports 1-6
// and A-G.  Pull-up MOS and open drain control are not here; the board decides
// what an undriven pin reads.

u8 Peripherals2320::port_read(u32 a) {
  if (a >= kPortData) return pdr_[a - kPortData];
  if (a < kPortPins) return pddr_[a - kPortDdr];  // write only on the chip, which reads back undefined
  const Port p = Port(a - kPortPins);
  const u8 pins = port_input_ ? port_input_(p) : 0;
  return u8((pins & ~pddr_[p]) | port_output(p));
}

void Peripherals2320::port_write(u32 a, u8 v) {
  if (a < kPortPins) { pddr_[a - kPortDdr] = v; return; }
  if (a < kPortData) return;  // PORTn is read only
  const Port p = Port(a - kPortData);
  pdr_[p] = v;
  if (port_write_) port_write_(p, v);
}

}  // namespace h8300h
