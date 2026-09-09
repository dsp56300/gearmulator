#include "cpu/h8500/sci.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {
enum Reg : u32 { kSmr = 0, kBrr = 1, kScr = 2, kTdr = 3, kSsr = 4, kRdr = 5 };
}  // namespace

Sci::Sci(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base, Sources src)
    : sched_(sched), clock_(clock), intc_(intc), base_(base), src_(src) {}

Sci::~Sci() {
  if (tx_event_) sched_.cancel(tx_event_);
  if (rx_event_) sched_.cancel(rx_event_);
}

void Sci::map(emu::IoMux& mux) { mux.assign(base_, 6, this); }

void Sci::reset() {
  if (tx_event_) sched_.cancel(tx_event_);
  if (rx_event_) sched_.cancel(rx_event_);
  tx_event_ = rx_event_ = 0;
  smr_ = 0x04;
  brr_ = 0xFF;
  scr_ = 0x0C;
  tdr_ = 0xFF;
  ssr_ = 0x87;
  rdr_ = 0;
  flags_read_ = 0;
  tsr_ = 0xFF;
  tsr_valid_ = false;
  tx_end_ = 0;
  rx_queue_.clear();
  rx_end_ = 0;
  now_ = 0;
  update_requests();
}

// ---------------------------------------------------------------------------
// Timing

u64 Sci::bit_states() const {
  if ((scr_ & kCke1) && ext_bit_states_) return ext_bit_states_;
  const u64 n = 1ull << (2 * (smr_ & 3));  // 4^n
  const u64 base = (smr_ & kCa) ? 4 : 32;
  return base * n * (u64(brr_) + 1);
}

u64 Sci::frame_states() const {
  if (smr_ & kCa) return 8 * bit_states();
  const unsigned data = (smr_ & kChr) ? 7 : 8;
  const unsigned parity = (smr_ & kPe) ? 1 : 0;
  const unsigned stop = (smr_ & kStop) ? 2 : 1;
  return (1 + data + parity + stop) * bit_states();
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
    if (tsr_valid_ && tx_sink_) tx_sink_(tsr_, ended);
    tsr_valid_ = false;
    // Continuous transmission: a byte written and TDRE cleared meanwhile
    // starts right at the frame boundary.
    if ((scr_ & kTe) && !(ssr_ & kTdre)) start_tx_frame(ended);
  }
  while (rx_end_ && now >= rx_end_) complete_rx_frame(rx_end_);
}

void Sci::start_tx_frame(u64 at) {
  tsr_ = (smr_ & kChr) && !(smr_ & kCa) ? u8(tdr_ & 0x7F) : tdr_;
  tsr_valid_ = true;
  tx_end_ = at + frame_states();
  ssr_ |= kTdre;  // TDR -> TSR empties the TDR
  schedule_tx(tx_end_);
  update_requests();
}

void Sci::complete_rx_frame(u64 at) {
  RxFrame f = rx_queue_.front();
  rx_queue_.pop_front();
  if (scr_ & kRe) {
    if (smr_ & kCa) { f.fer = false; f.per = false; }
    else if (!(smr_ & kPe)) f.per = false;
    if (ssr_ & kRdrf) {
      ssr_ |= kOrer;  // overrun: RSR contents are not transferred
      if (f.fer) ssr_ |= kFer;
      if (f.per) ssr_ |= kPer;
    } else {
      rdr_ = ((smr_ & kChr) && !(smr_ & kCa)) ? u8(f.byte & 0x7F) : f.byte;
      if (f.fer || f.per) {
        if (f.fer) ssr_ |= kFer;
        if (f.per) ssr_ |= kPer;
      } else {
        ssr_ |= kRdrf;
      }
    }
    if (rx_hook_) rx_hook_();
  }
  // The next frame on the wire, if any, starts right after this one.
  rx_end_ = rx_queue_.empty() ? 0 : at + frame_states();
  if (rx_end_) schedule_rx(rx_end_);
  update_requests();
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

void Sci::update_requests() {
  intc_.set_request(src_.txi, (scr_ & kTie) && (ssr_ & kTdre));
  intc_.set_request(src_.rxi, (scr_ & kRie) && (ssr_ & kRdrf));
  intc_.set_request(src_.eri, (scr_ & kRie) && (ssr_ & (kOrer | kFer | kPer)));
}

// ---------------------------------------------------------------------------
// Host side

void Sci::receive_byte(u8 byte, bool framing_error, bool parity_error) {
  const u64 now = clock_.now();
  sync(now);
  rx_queue_.push_back(RxFrame{byte, framing_error, parity_error});
  if (!rx_end_) {  // line idle: this frame starts now
    rx_end_ = now + frame_states();
    schedule_rx(rx_end_);
  }
}

void Sci::dtc_wrote_tdr(u8 value) {
  const u64 now = clock_.now();
  sync(now);
  tdr_ = value;
  ssr_ &= u8(~kTdre);
  if ((scr_ & kTe) && !tsr_busy(now)) start_tx_frame(now);
  update_requests();
}

u8 Sci::dtc_read_rdr() {
  ssr_ &= u8(~kRdrf);
  update_requests();
  return rdr_;
}

// ---------------------------------------------------------------------------
// Registers

u8 Sci::read8(u32 addr) {
  sync(clock_.now());
  switch (addr - base_) {
    case kSmr: return u8(smr_ | 0x04);
    case kBrr: return brr_;
    case kScr: return u8(scr_ | 0x0C);
    case kTdr: return tdr_;
    case kSsr:
      flags_read_ |= u8(ssr_ & 0xF8);
      return u8(ssr_ | 0x07);
    case kRdr: return rdr_;
    default: return 0xFF;
  }
}

void Sci::write8(u32 addr, u8 v) {
  const u64 now = clock_.now();
  sync(now);
  switch (addr - base_) {
    case kSmr: smr_ = u8(v | 0x04); break;
    case kBrr: brr_ = v; break;
    case kScr: {
      const u8 old = scr_;
      scr_ = u8(v | 0x0C);
      if ((scr_ & kTe) && !(old & kTe)) {
        // TE 0 -> 1: one frame of all ones goes out before data.
        tsr_valid_ = false;
        tx_end_ = now + frame_states();
        schedule_tx(tx_end_);
      } else if (!(scr_ & kTe) && (old & kTe)) {
        // TE cleared: TDRE is set if it was 0; the TSR finishes its frame.
        ssr_ |= kTdre;
      }
      update_requests();
      break;
    }
    case kTdr:
      // Writable regardless of TDRE (a pending byte is simply overwritten).
      tdr_ = v;
      break;
    case kSsr: {
      u8 cleared = 0;
      for (u8 bit = 0x08; bit; bit <<= 1)
        if (!(v & bit) && (flags_read_ & bit)) cleared |= bit;
      ssr_ &= u8(~cleared);
      flags_read_ &= u8(~cleared);
      if ((cleared & kTdre) && (scr_ & kTe) && !tsr_busy(now)) start_tx_frame(now);
      update_requests();
      break;
    }
    default:  // RDR is read-only
      break;
  }
}

}  // namespace h8500
