#include "cpu/m6801/mc6850.hpp"

#include <algorithm>

namespace m6801 {

using emu::u32;
using emu::u64;
using emu::u8;

namespace {
constexpr unsigned kDivide[3] = {1, 16, 64};  // CR1:CR0
// CR4-CR2: 7E2 7O2 7E1 7O1 8N2 8N1 8E1 8O1 (start bit included).
constexpr unsigned kFrameBits[8] = {11, 11, 10, 10, 11, 10, 11, 11};
}  // namespace

Mc6850::Mc6850(emu::Scheduler& sched, const emu::Clock& clock) : sched_(sched), clock_(clock) { power_on(); }

Mc6850::~Mc6850() {
  cancel(tx_event_);
  cancel(rx_event_);
}

void Mc6850::power_on() {
  cancel(tx_event_);
  cancel(rx_event_);
  control_ = 3;
  status_ = 0;
  tx_busy_ = rx_busy_ = false;
  rx_queue_.clear();
  update_irq();
}

u64 Mc6850::bit_q16() const { return u64(clock_q16_) * kDivide[control_ & 3]; }

unsigned Mc6850::frame_bits() const { return kFrameBits[(control_ >> 2) & 7]; }

u64 Mc6850::next_edge(u64 t) const {
  const u64 b = bit_q16();
  const u64 k = ((t - origin_) << 16) / b + 1;
  return origin_ + ((k * b + 0xFFFF) >> 16);
}

// IRQ: a received byte (or an overrun) with RIE, an empty transmit register
// with the transmit interrupt enabled (CR6:CR5 = 01).
void Mc6850::update_irq() {
  const bool rx = (control_ & 0x80) && (status_ & (kRdrf | kOvrn));
  const bool tx = ((control_ >> 5) & 3) == 1 && (status_ & kTdre);
  const bool was = (status_ & kIrq) != 0;
  const bool is = !in_reset() && (rx || tx);
  if (is) status_ |= kIrq;
  else status_ &= u8(~kIrq);
  if (is != was && irq_hook_) irq_hook_(is);
}

// Run the transitions due by `t` (a register access inside an instruction
// that started before them).
void Mc6850::sync(u64 t) {
  for (;;) {
    const bool tx = tx_event_ && tx_event_at_ <= t;
    const bool rx = rx_event_ && rx_event_at_ <= t;
    if (!tx && !rx) return;
    if (tx && (!rx || tx_event_at_ <= rx_event_at_)) {
      cancel(tx_event_);
      tx_event(tx_event_at_);
    } else {
      cancel(rx_event_);
      rx_event(rx_event_at_);
    }
  }
}

u8 Mc6850::read8(u32 addr) {
  const u64 t = clock_.now();
  sync(t);
  if (!(addr & 1)) return status_;
  // Reading the data clears RDRF and the overrun.
  const u8 v = rdr_;
  if (status_ & (kRdrf | kOvrn)) {
    status_ &= u8(~(kRdrf | kOvrn));
    update_irq();
  }
  return v;
}

void Mc6850::write8(u32 addr, u8 value) {
  const u64 t = clock_.now();
  sync(t);
  if (addr & 1) {
    // Transmit data: TDRE clears; the shifter takes it at the next bit edge.
    if (in_reset()) return;
    tdr_ = value;
    status_ &= u8(~kTdre);
    update_irq();
    tx_schedule(t);
    return;
  }
  const bool was_reset = in_reset();
  control_ = value;
  if (in_reset()) {
    // Master reset: both directions stop, the status clears.
    cancel(tx_event_);
    cancel(rx_event_);
    tx_busy_ = rx_busy_ = false;
    rx_queue_.clear();
    status_ = 0;
  } else if (was_reset) {
    // Out of reset: the dividers start, the transmit register is empty.
    origin_ = t;
    tx_ready_at_ = rx_line_free_ = t;
    status_ = kTdre;
  }
  update_irq();
}

// ---------------------------------------------------------------------------
// Transmitter

void Mc6850::tx_schedule(u64 t) {
  if (tx_event_ || tx_busy_ || in_reset() || (status_ & kTdre)) return;
  tx_event_at_ = std::max(tx_ready_at_, next_edge(t));
  tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
}

void Mc6850::tx_event(u64 when) {
  if (tx_busy_) {
    tx_busy_ = false;
    tx_ready_at_ = when;
    if (tx_hook_) tx_hook_(tx_shift_);
  }
  if (!(status_ & kTdre) && !in_reset()) {
    // The next byte moves to the shifter, TDRE sets.
    tx_shift_ = tdr_;
    status_ |= kTdre;
    tx_busy_ = true;
    tx_event_at_ = when + bits(frame_bits());
    tx_event_ = sched_.schedule(tx_event_at_, &on_tx, this);
  }
  update_irq();
}

void Mc6850::on_tx(void* self, u64 when, u64) {
  auto* a = static_cast<Mc6850*>(self);
  a->tx_event_ = 0;
  a->tx_event(when);
}

// ---------------------------------------------------------------------------
// Receiver

void Mc6850::receive(const u8* data, size_t len) {
  if (in_reset()) return;
  const u64 t = clock_.now();
  sync(t);
  rx_queue_.insert(rx_queue_.end(), data, data + len);
  rx_start(t);
}

void Mc6850::rx_start(u64 t) {
  if (rx_busy_ || rx_queue_.empty()) return;
  rx_shift_ = rx_queue_.front();
  rx_queue_.pop_front();
  rx_busy_ = true;
  rx_start_ = std::max(t, rx_line_free_);
  // The last stop bit is sampled in its middle.
  rx_event_at_ = rx_start_ + ((bit_q16() * (2 * frame_bits() - 1) / 2 + 0xFFFF) >> 16);
  rx_event_ = sched_.schedule(rx_event_at_, &on_rx, this);
}

void Mc6850::rx_event(u64) {
  rx_busy_ = false;
  rx_line_free_ = rx_start_ + bits(frame_bits());
  // Seven-bit words arrive without their top bit.
  const u8 data = ((control_ >> 2) & 7) < 4 ? u8(rx_shift_ & 0x7F) : rx_shift_;
  if (status_ & kRdrf) {
    status_ |= kOvrn;  // the byte is lost, the register keeps the old one
  } else {
    rdr_ = data;
    status_ |= kRdrf;
  }
  update_irq();
  rx_start(rx_line_free_);
}

void Mc6850::on_rx(void* self, u64 when, u64) {
  auto* a = static_cast<Mc6850*>(self);
  a->rx_event_ = 0;
  a->rx_event(when);
}

}  // namespace m6801
