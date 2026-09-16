// Slice budgets and pending-condition dispatch for cached threaded-code cores.
#pragma once
#include "common/device.hpp"
#include "common/sched.hpp"
#include "common/types.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define EMU_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define EMU_UNLIKELY(x) (x)
#endif

// Guaranteed tail call where the compiler offers it (clang); elsewhere the run
// loop dispatches and a handler simply returns the next cell.
#if defined(__clang__) && __has_cpp_attribute(clang::musttail)
#define EMU_HAS_MUSTTAIL 1
#define EMU_TAILCALL(expr) [[clang::musttail]] return (expr)
#else
#define EMU_HAS_MUSTTAIL 0
#define EMU_TAILCALL(expr) return (expr)
#endif

namespace emu {

// Charge `states` and continue with `next` unless the budget ran out (or a
// pending condition forced it negative), in which case return the resume point.
// `cpu` must expose `budget_` to the calling handler.
#if EMU_HAS_MUSTTAIL
#define EMU_END(cpu, next, states)                                  \
  do {                                                              \
    (cpu).budget_ -= ::emu::s32(states);                            \
    const auto* nx_ = (next);                                       \
    if (EMU_UNLIKELY((cpu).budget_ <= 0)) return nx_;               \
    [[clang::musttail]] return nx_->fn((cpu), nx_);                 \
  } while (0)
#define EMU_GOTO(cpu, ip) [[clang::musttail]] return (ip)->fn((cpu), (ip))
#else
#define EMU_END(cpu, next, states)                                  \
  do {                                                              \
    (cpu).budget_ -= ::emu::s32(states);                            \
    return (next);                                                  \
  } while (0)
// A jump runs its target at once, as a plain call. Handing it back to the run loop is not enough:
// the loop stops once the budget is spent and step() starts with none, so the step would end
// before the instruction just decoded ran. The depth stays bounded - a page cross leads to a
// cell, a fill to the handler it decoded, and handlers end in EMU_END.
#define EMU_GOTO(cpu, ip) return (ip)->fn((cpu), (ip))
#endif

class SliceCore : public CodeSink, public Clock {
 public:
  static constexpr s32 kForce = 1 << 28;
  static constexpr s32 kMaxSlice = 1 << 24;

  // Elapsed states.  Live while a slice is running (e.g. read by a device
  // during an MMIO access): resolves to the start of the current instruction.
  u64 total_states() const {
    return in_slice_ ? total_states_ + u64(s64(slice_len_) - s64(true_budget())) : total_states_;
  }
  u64 now() const override { return total_states(); }
  // At an instruction boundary: the bus was used by another master for
  // `states`; the CPU waited.
  void stall(u64 states) { total_states_ += states; }

  bool pending() const { return pending_ != 0; }
  // A slice is running: a call arriving now comes from an instruction in flight.
  bool in_slice() const { return in_slice_; }

  // End the running slice at absolute state `at` (at the first instruction
  // boundary at or after it).  The scheduler calls this when an event
  // scheduled during the slice is due before the slice would have ended, so
  // a peripheral event caused by a CPU write fires right after that write;
  // run() returns after such a slice (take_cut()) for the machine to fire it.
  void cut_slice(u64 at) {
    if (!in_slice_) return;
    const s64 left = true_budget();
    const s64 now = s64(total_states_) + (s64(slice_len_) - left);
    s64 remaining = s64(at) - now;
    if (remaining < 0) remaining = 0;
    if (remaining >= left) return;
    const s32 delta = s32(left - remaining);
    slice_len_ -= delta;
    budget_ -= delta;
    cut_ = true;
  }

 protected:
  void raise(u32 bit) {
    if (!pending_) budget_ -= kForce;
    pending_ |= bit;
  }
  void clear_pending(u32 bit) {
    if (pending_ & bit) {
      pending_ &= ~bit;
      if (!pending_) budget_ += kForce;
    }
  }
  void set_budget(s32 states) { budget_ = states - (pending_ ? kForce : 0); }
  s32 true_budget() const { return budget_ + (pending_ ? kForce : 0); }
  // Close the slice: fold the states used into the running total.
  u64 end_slice() {
    const s64 used = s64(slice_len_) - s64(true_budget());
    in_slice_ = false;
    total_states_ += u64(used);
    return u64(used);
  }
  void begin_slice(s32 slice) {
    set_budget(slice);
    slice_len_ = slice;
    in_slice_ = true;
  }

  bool take_cut() {
    const bool c = cut_;
    cut_ = false;
    return c;
  }

  s32 budget_ = 0;
  u32 pending_ = 0;
  s32 slice_len_ = 0;
  bool in_slice_ = false;
  bool cut_ = false;
  u64 total_states_ = 0;
};

}  // namespace emu
