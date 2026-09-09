// Event scheduler shared by all cores: the glue between a CPU's state budget and everything
// that happens "between instructions" (on-chip timers, serial ports, and
// external device models such as an LCD controller or an audio chip).
//
// Peripherals never tick per instruction.  Each one computes the absolute
// state count at which its next observable event happens (compare match,
// overflow, bit time, frame, sample...) and schedules a callback for it.  The
// machine runs the CPU until the earliest event, fires everything that is due,
// and repeats.  A callback receives both its nominal time and the current time
// (the CPU finishes the instruction in flight first, exactly like the hardware
// recognises an interrupt only at an instruction boundary), so a peripheral
// that needs cycle-exact state (a free-running counter read by the program)
// derives it from `now` when accessed rather than from ticks.
#pragma once
#include <algorithm>
#include <vector>

#include "common/types.hpp"

namespace emu {

// Source of the current time in states, for peripherals that evaluate their
// state lazily on register access (implemented by the CPU).
class Clock {
 public:
  virtual ~Clock() = default;
  virtual u64 now() const = 0;
};

class Scheduler {
 public:
  using Fn = void (*)(void* ctx, u64 when, u64 now);
  using EventId = u32;
  static constexpr u64 kNever = ~u64(0);

  // Called from schedule() when the new event is due before everything that
  // was pending: a CPU running a slice up to the previously earliest event
  // uses it to end that slice early, so an event a peripheral schedules in
  // response to a CPU write (a timer started with a tiny period, a DMA
  // request) fires after the instruction that caused it rather than at the
  // end of the slice.
  using EarlierHook = void (*)(void* ctx, u64 when);
  void set_earlier_hook(EarlierHook hook, void* ctx) { earlier_hook_ = hook; earlier_ctx_ = ctx; }

  // Schedule `fn(ctx, when, now)` at absolute state `when`.
  EventId schedule(u64 when, Fn fn, void* ctx) {
    const u64 before = next_time();
    const EventId id = next_id_++;
    heap_.push_back(Event{when, id, fn, ctx});
    std::push_heap(heap_.begin(), heap_.end(), Later{});
    if (earlier_hook_ && when < before) earlier_hook_(earlier_ctx_, when);
    return id;
  }
  // Cancel a pending event (no-op if already fired or unknown).
  void cancel(EventId id) {
    for (Event& e : heap_) if (e.id == id) { e.fn = nullptr; return; }
  }
  // Absolute time of the earliest pending event, or kNever.
  u64 next_time() {
    drop_cancelled();
    return heap_.empty() ? kNever : heap_.front().when;
  }
  // Fire every event with when <= now, in time order.
  void run_due(u64 now) {
    for (;;) {
      drop_cancelled();
      if (heap_.empty() || heap_.front().when > now) return;
      const Event e = heap_.front();
      std::pop_heap(heap_.begin(), heap_.end(), Later{});
      heap_.pop_back();
      e.fn(e.ctx, e.when, now);
    }
  }
  bool empty() const { return heap_.empty(); }
  size_t pending() const { return heap_.size(); }

 private:
  struct Event {
    u64 when;
    EventId id;
    Fn fn;
    void* ctx;
  };
  struct Later {  // min-heap on (when, id)
    bool operator()(const Event& a, const Event& b) const {
      return a.when != b.when ? a.when > b.when : a.id > b.id;
    }
  };
  void drop_cancelled() {
    while (!heap_.empty() && heap_.front().fn == nullptr) {
      std::pop_heap(heap_.begin(), heap_.end(), Later{});
      heap_.pop_back();
    }
  }

  std::vector<Event> heap_;
  EventId next_id_ = 1;
  EarlierHook earlier_hook_ = nullptr;
  void* earlier_ctx_ = nullptr;
};

}  // namespace emu
