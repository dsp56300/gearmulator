#include "cpu/sh2/sci.hpp"

namespace sh2 {

namespace {
enum Reg : u32 { kSmr = 0, kBrr = 1, kScr = 2, kTdr = 3, kSsr = 4, kRdr = 5 };
}  // namespace

Sci::Sci(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src)
    : sched_(sched), clock_(clock), intc_(intc), base_(base), src_(src) {
  reset();
}

Sci::~Sci() {
  if (tx_event_) sched_.cancel(tx_event_);
  if (rx_event_) sched_.cancel(rx_event_);
}

void Sci::map(emu::IoMux& mux) { mux.assign(base_, 6, this); }

// Power-on reset values of 12.2 (standby leaves the same state).
void Sci::reset() {
  if (tx_event_) sched_.cancel(tx_event_);
  if (rx_event_) sched_.cancel(rx_event_);
  tx_event_ = rx_event_ = 0;
  smr_ = 0x00;
  brr_ = 0xFF;
  scr_ = 0x00;
  tdr_ = 0xFF;
  ssr_ = kTdre | kTend;  // H'84
  rdr_ = 0;
  flags_read_ = 0;
  tsr_ = 0xFF;
  tsr_mpb_ = false;
  tx_end_ = 0;
  rx_queue_.clear();
  rx_end_ = 0;
  now_ = 0;
  update_requests();
}

// ---------------------------------------------------------------------------
// Timing (12.2.8)

u64 Sci::bit_states() const {
  if ((scr_ & kCke1) && ext_bit_states_) return ext_bit_states_;
  const u64 n = 1ull << (2 * (smr_ & 3));  // 4^n for CKS1-0 = n (phi, phi/4, phi/16, phi/64)
  const u64 base = (smr_ & kCa) ? 4 : 32;
  return base * n * (u64(brr_) + 1);
}

u64 Sci::frame_states() const {
  if (smr_ & kCa) return 8 * bit_states();  // figure 12.14: 8 data bits, nothing else
  // Table 12.10: start + data + (parity | MPB) + stop.  MP overrides PE.
  const unsigned data = (smr_ & kChr) ? 7 : 8;
  const unsigned extra = (smr_ & (kMp | kPe)) ? 1 : 0;
  const unsigned stop = (smr_ & kStop) ? 2 : 1;
  return (1 + data + extra + stop) * bit_states();
}

void Sci::schedule_tx(u64 when) {
  if (tx_event_) sched_.cancel(tx_event_);
  tx_event_ = sched_.schedule(when, &Sci::on_tx_event, this);
}

void Sci::schedule_rx(u64 when) {
  if (rx_event_) sched_.cancel(rx_event_);
  rx_event_ = sched_.schedule(when, &Sci::on_rx_event, this);
}

// Bring the transmit / receive state up to `now`: complete frames whose end
// time has passed.  Events normally land exactly on those times; register
// accesses in between may arrive first.
void Sci::sync(u64 now) {
  if (now < now_) return;
  now_ = now;
  if (tx_end_ && now >= tx_end_) {
    const u64 ended = tx_end_;
    tx_end_ = 0;
    if (tx_sink_) tx_sink_(tsr_, tsr_mpb_, ended);
    // 12.3.2 step 3 (at the frame end here): TDRE = 0 -> the next frame
    // follows back to back; TDRE = 1 -> TEND, marking.
    if ((scr_ & kTe) && !(ssr_ & kTdre) && tx_allowed()) start_tx_frame(ended);
    else if (ssr_ & kTdre) ssr_ |= kTend;
  }
  while (rx_end_ && now >= rx_end_) complete_rx_frame(rx_end_);
  update_requests();
}

// TDR -> TSR: the frame starts at `at`, TDRE goes back to 1 (TXI).
void Sci::start_tx_frame(u64 at) {
  tsr_ = seven_bit() ? u8(tdr_ & 0x7F) : tdr_;  // 12.2.5 CHR: TDR bit 7 is not transmitted
  tsr_mpb_ = mp_format() && (ssr_ & kMpbt);     // 12.2.7 MPBT: ignored outside the MP format
  tx_end_ = at + frame_states();
  ssr_ |= kTdre;
  schedule_tx(tx_end_);
}

// Start a frame if the transmitter has data (TDRE = 0), is enabled and idle.
void Sci::try_start_tx(u64 now) {
  if ((scr_ & kTe) && !(ssr_ & kTdre) && !tsr_busy(now) && tx_allowed()) start_tx_frame(now);
}

void Sci::complete_rx_frame(u64 at) {
  RxFrame f = rx_queue_.front();
  rx_queue_.pop_front();
  if (scr_ & kRe) {  // 12.2.6 RE = 0: frames pass by, flags and RDR untouched
    if (smr_ & kCa) { f.fer = f.per = false; f.mpb = false; }  // 12.3.4: only ORER exists
    else if (mp_format() || !(smr_ & kPe)) f.per = false;      // 12.2.5: parity ignored with MP or PE = 0
    bool skip = false;
    if (mp_format()) {
      // 12.2.7 MPB latches the frame's multiprocessor bit (RE = 1 only).
      ssr_ = u8((ssr_ & ~kMpb) | (f.mpb ? kMpb : 0));
      if (scr_ & kMpie) {
        // 12.2.6 MPIE: data frames are skipped (no RSR -> RDR, no RDRF / FER /
        // ORER); an ID frame clears MPIE and is received normally.
        if (f.mpb) scr_ &= u8(~kMpie);
        else skip = true;
      }
    }
    if (!skip) {
      if (ssr_ & kErrors) {
        // 12.3.2 note: receiving is disabled while an error flag is set; the
        // receiver keeps checking, so a persisting break re-sets FER (12.5.3).
        if (f.fer) ssr_ |= kFer;
        if (f.per) ssr_ |= kPer;
      } else if (ssr_ & kRdrf) {
        // Table 12.13: overrun, RSR not transferred, RDR keeps the old data.
        ssr_ |= kOrer;
        if (f.fer) ssr_ |= kFer;
        if (f.per) ssr_ |= kPer;
      } else {
        rdr_ = seven_bit() ? u8(f.byte & 0x7F) : f.byte;
        if (f.fer) ssr_ |= kFer;
        if (f.per) ssr_ |= kPer;
        if (!f.fer && !f.per) ssr_ |= kRdrf;  // table 12.11: data transferred, RDRF only when clean
      }
    }
  }
  // The next frame on the wire, if any, starts right after this one.
  rx_end_ = rx_queue_.empty() ? 0 : at + frame_states();
  if (rx_end_) schedule_rx(rx_end_);
}

void Sci::on_tx_event(void* self, u64 /*when*/, u64 now) {
  auto* s = static_cast<Sci*>(self);
  s->tx_event_ = 0;
  s->sync(now);
}

void Sci::on_rx_event(void* self, u64 /*when*/, u64 now) {
  auto* s = static_cast<Sci*>(self);
  s->rx_event_ = 0;
  s->sync(now);
}

// Table 12.12; all four are level requests that follow flag && enable.
void Sci::update_requests() {
  intc_.set_request(src_.txi, (scr_ & kTie) && (ssr_ & kTdre));
  intc_.set_request(src_.rxi, (scr_ & kRie) && (ssr_ & kRdrf));
  intc_.set_request(src_.eri, (scr_ & kRie) && (ssr_ & kErrors));
  intc_.set_request(src_.tei, (scr_ & kTeie) && (ssr_ & kTend));
}

// ---------------------------------------------------------------------------
// Host side

void Sci::receive_byte(u8 byte, bool mpb, bool framing_error, bool parity_error) {
  const u64 now = clock_.now();
  sync(now);
  rx_queue_.push_back(RxFrame{byte, mpb, framing_error, parity_error});
  if (!rx_end_) {  // line idle: this frame starts now
    rx_end_ = now + frame_states();
    schedule_rx(rx_end_);
  }
}

// 12.2.7 TDRE / TEND: a DMAC write to TDR clears both.
void Sci::dma_wrote_tdr(u8 value) {
  const u64 now = clock_.now();
  sync(now);
  tdr_ = value;
  if (scr_ & kTe) {  // TDRE is locked at 1 while TE = 0 (12.2.6)
    ssr_ &= u8(~(kTdre | kTend));
    try_start_tx(now);
  }
  update_requests();
}

// 12.2.7 RDRF: a DMAC read of RDR clears it.
u8 Sci::dma_read_rdr() {
  sync(clock_.now());
  ssr_ &= u8(~kRdrf);
  update_requests();
  return rdr_;
}

// ---------------------------------------------------------------------------
// Registers

u8 Sci::read8(u32 addr) {
  sync(clock_.now());
  switch (addr - base_) {
    case kSmr: return smr_;
    case kBrr: return brr_;
    case kScr: return scr_;
    case kTdr: return tdr_;
    case kSsr:
      flags_read_ |= u8(ssr_ & 0xF8);  // flags seen as 1 become clearable
      return ssr_;
    case kRdr: return rdr_;
    default: return 0xFF;  // "Do not access empty addresses" (table 12.2)
  }
}

void Sci::write8(u32 addr, u8 v) {
  const u64 now = clock_.now();
  sync(now);
  switch (addr - base_) {
    case kSmr: smr_ = v; break;
    case kBrr: brr_ = v; break;
    case kScr: {
      const u8 old = scr_;
      scr_ = v;
      if (!(scr_ & kTe) && (old & kTe)) {
        // TE 1 -> 0: the transmit section is initialised regardless of its
        // status (12.5.4): the frame in flight is abandoned, TDRE and TEND
        // are set (12.2.7).
        if (tx_event_) { sched_.cancel(tx_event_); tx_event_ = 0; }
        tx_end_ = 0;
        ssr_ |= kTdre | kTend;
      }
      // TE 0 -> 1: marking until TDRE is cleared (12.3.2 initialisation
      // step 4); TDRE cannot be 0 here because it is locked at 1 while TE = 0.
      update_requests();
      break;
    }
    case kTdr:
      // Writable regardless of TDRE (12.5.1: the pending byte is overwritten).
      tdr_ = v;
      break;
    case kSsr: {
      // TDRE, RDRF, ORER, FER, PER: writing 0 clears a flag read as 1.  TEND
      // and MPB are read-only; MPBT is a plain read/write bit.
      u8 cleared = 0;
      for (u8 bit = 0x08; bit; bit <<= 1)
        if (!(v & bit) && (flags_read_ & bit)) cleared |= bit;
      if (!(scr_ & kTe)) cleared &= u8(~kTdre);  // locked at 1 while TE = 0 (12.2.6)
      ssr_ &= u8(~cleared);
      flags_read_ &= u8(~cleared);
      ssr_ = u8((ssr_ & ~kMpbt) | (v & kMpbt));
      if (cleared & kTdre) ssr_ &= u8(~kTend);  // 12.2.7 TEND clearing condition
      // A cleared TDRE starts the frame if the TSR is free; clearing a receive
      // error flag releases a transmission held off by 12.5.5.
      if (cleared & (kTdre | kErrors)) try_start_tx(now);
      update_requests();
      break;
    }
    default:  // RDR is read-only; empty addresses are ignored
      break;
  }
}

}  // namespace sh2
