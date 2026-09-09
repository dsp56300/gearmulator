// A/D converters of the SH7014 family.
//
// Two different modules share this file: the high speed A/D converter of
// the SH7014 (hardware manual section 13) and the mid-speed A/D converter of
// the SH7016/SH7017 (section 14).  Both are 10-bit, eight-channel converters
// with a sample-and-hold stage, single and scan modes, an MTU conversion
// start trigger and one interrupt source, ADI = ADF & ADIE (vector 136 on
// the SH7014, 138 on the SH7016/17; IPRG bits 15-12).  ADF is cleared by
// reading it as 1 and then writing 0, or by the DMAC (dmac_activated()).
//
// Timing model (common to both): the converter never ticks.  A run is a
// sequence of "steps" at absolute state counts - the sampling instant, at
// which the analog value is latched (set_input() value or the host sampler,
// evaluated right then), and the conversion end, at which the result lands
// in its data register.  The next step time is kept in next_at_; one
// scheduler event covers it and every register access first replays all
// steps that are due (sync), so the registers are exact even between events.
//
// Analog approximations: the converters are ideal (no offset, full-scale or
// nonlinearity error - sections 13.6 / 14.6), the hold is instantaneous and
// the value latched is whatever the host reports at the sampling instant;
// pin voltage ranges, AVCC/AVSS and the source impedance notes are ignored.
// Standby-mode initialisation of the registers is left to the integrator.
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "common/sched.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/intc.hpp"

namespace sh2 {

// ---------------------------------------------------------------------------
// SH7014 high speed A/D converter (section 13).
//
// Registers: ADCSR H'FFFF83E0 (ADF ADIE ADST CKS GRP CH2 CH1 CH0), ADCR
// H'FFFF83E1 (- PWR TRGS1 TRGS0 SCAN DSMP BUFE1 BUFE0), ADDRA-ADDRH
// H'FFFF83F0-H'FFFF83FF (16-bit, right-justified: AD9-8 in the high byte,
// AD7-0 in the low byte per appendix A; section 13.3's remark that a byte
// read returns AD9-AD2 contradicts the register layout and is not followed).
// ANn -> ADDR n, except during buffer operation.
//
// Modes (13.4): select (GRP = 0, one channel) or group (GRP = 1, AN0-ANn);
// single (one round, ADST auto-cleared) or scan (rounds repeat until ADST
// is cleared; with ADIE = 1 the converter pauses when ADF is set and resumes
// when ADF is cleared - 13.5).  Buffer operation (13.4.5): the AN0 (and AN1
// for BUFE = 10) results shift through ADDRA-ADDRD; in group mode the round
// follows tables 13.4/13.5 (buffer channels are skipped when the group
// reaches past them, otherwise the source is converted once per stage); in
// select mode every start converts one stage and ADF is set after the
// number of stages the CH bits select (table 13.4), counted in buf_count_
// until BUFE is written 00.  Simultaneous sampling (DSMP, 13.4.6): channel
// pairs of the group are held together and converted in order.
//
// Timing (table 13.7, states of phi): the first conversion of a run holds
// the input tD + tSPL = 21.5 (CKS = 0) / 41.5 (CKS = 1) states after the
// start and ends after tCONV = 42.5 / 82.5 states; conversions in
// succession take tCP = 20 / 40 states each, their sampling overlapping the
// previous conversion (the two S&H circuits of figure 13.1), so the next
// channel is held at the instant the previous one completes.  Half states
// are rounded up to the next state boundary (22 / 43, 42 / 83).  With
// DSMP the next pair is resampled for tSPL after the previous pair ends.
// Power (13.4.7): the analog circuit needs 200 states after power-up; with
// PWR = 0 it is powered at ADST = 1 and switched off when the conversion
// ends, with PWR = 1 it stays on from the PWR write.  The trigger start
// (TRGS = 01) is trigger() - the MTU TTGE request; there is no ADTRG pin on
// these parts.  Software can set ADST in any TRGS setting.
class AdcHighSpeed final : public Device {
 public:
  static constexpr u32 kAdcsr = 0xFFFF83E0u, kAdcr = 0xFFFF83E1u, kAddr = 0xFFFF83F0u;
  // ADCSR
  static constexpr u8 kAdf = 0x80, kAdie = 0x40, kAdst = 0x20, kCks = 0x10, kGrp = 0x08, kChMask = 0x07;
  // ADCR
  static constexpr u8 kPwr = 0x40, kTrgsMask = 0x30, kTrgsMtu = 0x10, kScan = 0x08, kDsmp = 0x04, kBufeMask = 0x03;
  // Table 13.7, indexed by CKS.
  static constexpr u64 kFirstHold[2] = {22, 42};  // tD + tSPL (21.5 / 41.5)
  static constexpr u64 kFirstEnd[2] = {43, 83};   // tCONV (42.5 / 82.5)
  static constexpr u64 kNext[2] = {20, 40};       // tCP, conversions in succession
  static constexpr u64 kSample[2] = {20, 40};     // tSPL
  static constexpr u64 kPowerUp = 200;            // 13.4.7 analog power-up time

  using Sampler = std::function<u16(unsigned channel)>;  // returns a 10-bit value

  AdcHighSpeed(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc);
  ~AdcHighSpeed() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;

  // --- host side -----------------------------------------------------------
  // Analog value of ANn (10 bits) from now on; a sampling instant already due
  // still sees the previous value.
  void set_input(unsigned channel, u16 value10);
  void set_sampler(Sampler s) { sampler_ = std::move(s); }
  // MTU conversion start trigger (TTGE): starts a conversion when TRGS = 01.
  void trigger();
  // The DMAC accepted the ADI request: ADF is cleared when the data register
  // is read (13.4.5 "ADF Flag Clearing"; any ADDR read counts here).
  void dmac_activated();

  u16 result(unsigned i) const { return addr_[i & 7]; }
  u8 adcsr() { sync(clock_.now()); return adcsr_; }
  u8 adcr() const { return adcr_; }
  bool converting() { sync(clock_.now()); return phase_ != Phase::kIdle; }
  bool powered() const { return power_on_at_ != emu::Scheduler::kNever; }

 private:
  enum class Phase : u8 { kIdle, kSampling, kConverting };

  static void on_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void arm();
  void start_run(u64 at);
  void stop();
  void build_round();
  void step(u64 at);
  void next_channel(u64 at);
  void round_done(u64 at);
  void latch(unsigned pos);
  void store(unsigned channel, u16 v);
  void clear_adf(u64 now);
  unsigned buf_target() const;
  unsigned cks() const { return (adcsr_ & kCks) ? 1 : 0; }
  bool dsmp() const { return (adcsr_ & kGrp) && (adcr_ & kDsmp); }
  void update_request();

  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  emu::Scheduler::EventId event_ = 0;
  u64 event_at_ = 0;
  Sampler sampler_;

  std::array<u16, 8> addr_{};
  std::array<u16, 8> inputs_{};
  u8 adcsr_ = 0;
  u8 adcr_ = 0;
  bool adf_read_ = false;
  bool dma_clear_pending_ = false;

  Phase phase_ = Phase::kIdle;
  bool first_ = false;       // the step in flight is the first conversion of a run
  bool suspended_ = false;   // scan mode paused with ADF set (13.5)
  u64 next_at_ = 0;          // time of the next step while phase_ != kIdle
  u64 power_on_at_ = emu::Scheduler::kNever;  // analog circuit power-up instant (kNever = off)
  std::array<u8, 8> round_{};  // channels of one round, in order
  unsigned round_len_ = 0;
  unsigned pos_ = 0;           // index into round_ of the channel being converted
  std::array<u16, 2> held_{};  // latched samples (pairs with DSMP)
  unsigned buf_count_ = 0;     // buffer stages filled since BUFE was written 00
  u64 now_ = 0;
};

// ---------------------------------------------------------------------------
// SH7016/SH7017 mid-speed A/D converter (section 14).
//
// Registers at H'FFFF8420: ADDRA-ADDRD (+0..+7, 16-bit, left-justified: AD9-2
// in the high byte, AD1-0 in bits 7-6 of the low byte; the low byte is read
// through TEMP after the high byte - 14.3), ADCSR +8 (ADF ADIE ADST SCAN CKS
// CH2 CH1 CH0), ADCR +9 (TRGE; bits 6-0 read 1).  CH2 selects the channel
// group (AN0-3 -> ADDRA-D, AN4-7 -> ADDRA-D); single mode converts channel
// CH, scan mode converts the first CH1-0 + 1 channels of the group in order
// and repeats until ADST is cleared (table in 14.2.2), setting ADF after
// each round.
//
// Timing (14.4.3, table 14.4): the first conversion of a run takes 266
// (CKS = 0) / 134 (CKS = 1) states - the table maximum, tD = 17 / 9 - with
// the input held after tD + tSPL = 81 / 41 states; conversions in
// succession take 256 / 128 states with the input held after tSPL = 64 / 32.
// The actual tD varies with the ADCSR write phase (10-17 states); the model
// always uses the maximum.  trigger() is the MTU start (TRGE = 1).
class AdcMidSpeed final : public Device {
 public:
  static constexpr u32 kBase = 0xFFFF8420u;
  static constexpr u32 kAdcsr = kBase + 8, kAdcr = kBase + 9;
  // ADCSR
  static constexpr u8 kAdf = 0x80, kAdie = 0x40, kAdst = 0x20, kScan = 0x10, kCks = 0x08, kChMask = 0x07;
  // ADCR
  static constexpr u8 kTrge = 0x80;
  // Table 14.4, indexed by CKS.
  static constexpr u64 kFirstHold[2] = {81, 41};   // tD(max) + tSPL
  static constexpr u64 kFirstEnd[2] = {266, 134};  // tCONV(max)
  static constexpr u64 kNextHold[2] = {64, 32};    // tSPL
  static constexpr u64 kNextEnd[2] = {256, 128};   // second and later conversions

  using Sampler = std::function<u16(unsigned channel)>;  // returns a 10-bit value

  // base: ADDRA (H'FFFF8420 on the SH7016/17, H'5FFFEE0 on the SH7034); the
  // SH7042's two units interleave their control registers (ADDRA0 H'FFFF8400,
  // ADCSR0 H'FFFF8410, ADCR0 H'FFFF8412; unit 1 at +8, +1, +1), so ADCSR and
  // ADCR addresses and the interrupt source are parameters too.
  AdcMidSpeed(emu::Scheduler& sched, const emu::Clock& clock, Intc& intc, u32 base = kBase, u32 adcsr = 0, u32 adcr = 0,
              IrqSrc src = IrqSrc::Adi);
  ~AdcMidSpeed() override;

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;

  // --- host side -----------------------------------------------------------
  // Analog value of ANn (10 bits) from now on; a sampling instant already due
  // still sees the previous value.
  void set_input(unsigned channel, u16 value10);
  void set_sampler(Sampler s) { sampler_ = std::move(s); }
  // MTU conversion start trigger: starts a conversion when TRGE = 1 (14.4.4).
  void trigger();
  // The DMAC accepted the ADI request: the next access to a register of the
  // module clears ADF (14.2.2).
  void dmac_activated();

  u16 result(unsigned i) const { return addr_[i & 3]; }
  u8 adcsr() { sync(clock_.now()); return adcsr_; }
  bool converting() { sync(clock_.now()); return phase_ != Phase::kIdle; }

 private:
  enum class Phase : u8 { kIdle, kSampling, kConverting };

  static void on_event(void* self, u64 when, u64 now);
  void sync(u64 now);
  void arm();
  void start_run(u64 at);
  void stop();
  void step(u64 at);
  void access();
  unsigned cks() const { return (adcsr_ & kCks) ? 1 : 0; }
  unsigned channel_of(unsigned pos) const { return (adcsr_ & 4) | pos; }
  unsigned round_len() const { return (adcsr_ & kScan) ? (adcsr_ & 3) + 1 : 1; }
  void update_request();

  u32 base_, adcsr_addr_, adcr_addr_;
  IrqSrc src_;
  emu::Scheduler& sched_;
  const emu::Clock& clock_;
  Intc& intc_;
  emu::Scheduler::EventId event_ = 0;
  u64 event_at_ = 0;
  Sampler sampler_;

  std::array<u16, 4> addr_{};
  std::array<u16, 8> inputs_{};
  u8 adcsr_ = 0;
  u8 adcr_ = 0;  // only TRGE stored
  u8 temp_ = 0;
  bool adf_read_ = false;
  bool dma_clear_pending_ = false;

  Phase phase_ = Phase::kIdle;
  bool first_ = false;
  u64 next_at_ = 0;
  unsigned channel_ = 0;  // channel being converted
  u16 held_ = 0;
  u64 now_ = 0;
};

}  // namespace sh2
