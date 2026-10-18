#include "tvf_emulator.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// MB87424 TVF chip emulator
// 32 voices amplitude and state-variable filter with resonance and params interpolation
// 16 cycles per voice per sample
//
// Internally, the die features the following components:
// - multiplier: ONE 14x18 array (top 25 bits, >>7), shared by TVA/ring/mix and
//   the SVF integrators -- see mulAudio/mulState for the port semantics
// - 24bit adder (DSP accumulator)
// - 18bit adder (param interpolation / envelope)
// - RAM A: 24bit x 128word (4 words/voice: filter state, damping, mode)
// - RAM B: 20bit x 192word (6 words/voice: cutoff, gain, steps, targets)
//

// The host interface exposes 32 bit aligned register slots, paged by voice, the
// same way the EP does. The addresses from 0x00 to 0x3f are the register file,
// the addresses from 0x40 to 0x7f select which register the 24 bit read port
// mirrors. Unlike the EP, the write context and the read context are separate:
// 0x40 latches BOTH (it is the destination selector and the selector for
// register 0x00), every other selector only re-points the read port.

// Host interface map (on write):
// 0x00: SVF integrator, LP tap      (24 bit, per voice)
// 0x04: SVF companion state (HP)    (24 bit, per voice)
// 0x08: Damping + filter flags      (24 bit, per voice)
//      b0:      ring sum (also sum the ring's B input into its output)
//      b1-b2:   filter mode (0=LPF, 1=HPF, 2=BPF, 3=unknown/silent)
//      b3-b4:   structure (4 topologies)
//      b5-b7:   never written by firmware, unprobed
//      b8-b20:  damping / resonance (Q12, 0x1000 == 1.0, 0x0000 == self-osc)
// 0x0c: SVF integrator, BP tap      (24 bit, per voice)
// 0x10: Voice enable mask, high     (16 bit, global)
// 0x12: Voice enable mask, low      (16 bit, global)
// 0x14: Ramp enable                 (16 bit, global)
//      target loads and the masked follow run iff bit1 & bit2 & (bit3|bit4);
//      firmware boots 0x0017, other firmware uses 0x001f; bit0 belongs to something else
// 0x16: Boot config, value 0        (16 bit, global, no observed effect)
// 0x18: Glide rate                  (16 bit, global, low byte only)
// 0x1a-0x1e: unused (no observed effect at 0x0001)
// 0x20: Cutoff current              (20 bit, per voice)
// 0x24: Gain current                (20 bit, per voice)
// 0x28: Cutoff glide step           (20 bit, per voice, signed, ramp-owned)
// 0x2c: Gain glide step             (20 bit, per voice, signed, ramp-owned)
// 0x30: Cutoff target               (20 bit, per voice)
//      b19:     1 = apply instantly, 0 = start the fixed-time glide
//      b4-b18:  destination (Q14, cutoff word << 4)
// 0x34: Gain target                 (20 bit, per voice, same layout)
// 0x38: reserved/limit (reads railed 0x0fffff, no firmware writer)
// 0x3c: reserved/limit (reads railed 0x0fffff, no firmware writer)
// 0x40-0x7c: Mirror of 0x00-0x3c, write voice number to read address-0x40.
//      Bit1 is ignored everywhere, bit4 additionally in the 0x40 block, so e.g.
//      0x4a aliases 0x48. 0x40 doubles as the WRITE context select.

// Host interface map (on read):
// 0x00: Read port, lane A (low 16 bits of the selected 24 bit value)
// 0x02: Read port, lane B (high 8 bits)
// ...aliased across the whole window; only address bit A1 decodes.

// 24 bit register packing (0x00-0x0c), one RAM A word over two bus words:
// on write:    reg + 0 = val >> 8
//              reg + 2 = (val & 0xff) << 8      (low byte of that word unused)
// on readback: reg + 0 = val & 0xffff
//              reg + 2 = val >> 16
// The firmware writes reg+2 FIRST and reg+0 second at every site (ROM 0x355BA,
// 0x3BB02, 0x32B5A), so the reg+0 write is the commit point.
//

// 20 bit register packing (0x20-0x3c), one RAM B word over two bus words:
// on write:    reg + 0 = val >> 4
//              reg + 2 = (val & 0xf) << 12
// on readback: reg + 0 = val & 0xffff
//              reg + 2 = val >> 16

namespace tvfLib
{

namespace {

constexpr int mulUnity = 0x4000;

// THE multiplier: one 14x18 array shared by everything (TVA, ring, mix, both
// SVF integrators), feeding the one 24-bit accumulator. Everything in the
// accumulator/RAM domain is DIE SCALE (working << 6). The signal port takes
// an 18-bit WORKING value -- bus audio directly, die-scale words as word>>6
// (their 6 fraction bits exist only for the accumulator and are dropped at
// the port; the shifts are explicit at the call sites). The product = top 25
// bits of the array = (signal * Q14coeff) >> 8, landing at die scale.
inline int64_t mul18x14(int64_t signal_18, int64_t coeff_14) {
  return (signal_18 * coeff_14) >> 8;
}

inline int32_t signExtend24(uint32_t raw) {
  return int32_t(raw ^ 0x800000) - 0x800000;
}

inline int32_t sat24(int64_t value) {
  constexpr int64_t kSampleMax = (1 << 23) - 1;   // +8388607
  constexpr int64_t kSampleMin = -(1 << 23);      // -8388608
  if (value > kSampleMax) return kSampleMax;
  if (value < kSampleMin) return kSampleMin;
  return int32_t(value);
}

// The subtract form truncates the product toward -inf FIRST, then subtracts
// -- that dead zone below one die LSB is silicon-measured (damping residual
// floors of 4096/damp).
inline int32_t mac(int64_t acc, int64_t sig_18, int64_t coeff_14) {
  return sat24(acc + mul18x14(sig_18, coeff_14));
}
inline int32_t macSub(int64_t acc, int64_t sig_18, int64_t coeff_14) {
  return sat24(acc - mul18x14(sig_18, coeff_14));
}

// 24 bit RAM A words: reg + 0 = val >> 8, reg + 2 = (val & 0xff) << 8.
void writeReg24Word0(uint32_t *dest, uint16_t value) {
  *dest = (*dest & 0x0000ff) | (uint32_t(value) << 8);
}
void writeReg24Word2(uint32_t *dest, uint16_t value) {
  *dest = (*dest & 0xffff00) | (value >> 8);
}
// The SVF state slots hold signed samples; they are kept sign-extended from
// bit 23 so the ramp can use them directly.
void writeReg24Word0(int32_t *dest, uint16_t value) {
  uint32_t raw = uint32_t(*dest) & 0xffffff;
  writeReg24Word0(&raw, value);
  *dest = signExtend24(raw);
}
void writeReg24Word2(int32_t *dest, uint16_t value) {
  uint32_t raw = uint32_t(*dest) & 0xffffff;
  writeReg24Word2(&raw, value);
  *dest = signExtend24(raw);
}

// 20 bit RAM B words: reg + 0 = val >> 4, reg + 2 = (val & 0xf) << 12.
void writeReg20Word0(uint32_t *dest, uint16_t value) {
  *dest = (*dest & 0x0000f) | (uint32_t(value) << 4);
}
void writeReg20Word2(uint32_t *dest, uint16_t value) {
  *dest = (*dest & 0xffff0) | ((value >> 12) & 0xf);
}

}  // namespace

TVFEmulator::TVFEmulator(const bool littleEndianBus) : little_endian_bus_(littleEndianBus) { reset(); }

void TVFEmulator::reset() {
  memset(global_, 0, sizeof(global_));
  memset(voices_, 0, sizeof(voices_));
  active_mask_ = 0;
  ramp_enable_ = 0;
  glide_rate_ = 0;
  selected_voice_ = 0;
  prev_active_mask_ = 0;
  glide_tick_phase_ = 0;
  port_reg_ = 0;
  port_voice_ = 0;
  port_latch_ = 0;
}

bool TVFEmulator::rampEnabled() const {
  return (ramp_enable_ & 0x02) && (ramp_enable_ & 0x04) &&
         (ramp_enable_ & 0x18);
}

int32_t TVFEmulator::glideStep(int32_t dist20, uint8_t rate) {
  if ((rate & 0x1f) == 0) return dist20 | 1;
  if ((rate & 0x1f) == 0x1f) return int32_t(int64_t(dist20) >> 10) | 1;
  static const int kMant[4] = {16, 13, 11, 9};
  const int shift = (rate >> 2) & 7;
  const int64_t step = (int64_t(dist20) * kMant[rate & 3]) >> (6 + shift);
  return int32_t(step) | 1;
}

void TVFEmulator::armGlide(int32_t cur20, int32_t tgt20, uint8_t rate,
                           int32_t* step20, bool* gliding) {
  const int32_t dist = tgt20 - cur20;
  if (dist == 0) { *gliding = false; return; }
  *step20 = glideStep(dist, rate);
  *gliding = true;
}

void TVFEmulator::write(uint16_t offset, uint8_t value) {
  offset &= 0x7f;
  global_[offset] = value;
  if (offset & 1) {
    const uint16_t word = little_endian_bus_
        ? uint16_t(global_[offset - 1]) | (uint16_t(value) << 8)
        : (uint16_t(global_[offset - 1]) << 8) | uint16_t(value);
    write16(uint16_t(offset - 1), word);
  }
}

void TVFEmulator::write16(uint16_t offset, uint16_t value) {
  // global control block
  switch (offset) {
    case 0x10: active_mask_ = (active_mask_ & 0x0000ffff) | (uint32_t(value) << 16); return;
    case 0x12: active_mask_ = (active_mask_ & 0xffff0000) | value; return;
    case 0x14: ramp_enable_ = value; return;
    case 0x16: return;
    case 0x18: glide_rate_ = uint8_t(value); return;
    case 0x1a: return;
    case 0x1c: return;
    case 0x1e: return;
    default: break;
  }

  // voice read/select
  if (offset >= 0x40) {
    selected_voice_ = value & 0x1f;
    port_reg_ = offset < 0x60 ? uint8_t(offset & 0x0c)
                              : uint8_t(0x20 | (offset & 0x1c));
    port_voice_ = uint8_t(value & 0x1f);
    port_latch_ = readPort24();  // silicon latches the value at select time
    return;
  }

  Voice& voice = voices_[selected_voice_];
  const bool ramp = rampEnabled();
  const bool active = (active_mask_ & (1u << selected_voice_)) != 0;

  // per-voice block
  switch (offset) {
  case 0x00: writeReg24Word0(&voice.svfStateBand_00, value); break;
  case 0x02: writeReg24Word2(&voice.svfStateBand_00, value); break;
  case 0x04: writeReg24Word0(&voice.svfStateHigh_04, value); break;
  case 0x06: writeReg24Word2(&voice.svfStateHigh_04, value); break;

  case 0x08: writeReg24Word0(&voice.filterControl_08, value); break;
  case 0x0a: writeReg24Word2(&voice.filterControl_08, value); break;

  case 0x0c: writeReg24Word0(&voice.svfStateLow_0c, value); break;
  case 0x0e: writeReg24Word2(&voice.svfStateLow_0c, value); break;

  case 0x20:
    if (!ramp) break;
    writeReg20Word0(&voice.cutoffCurrent_20, value);
    voice.cutoff_gliding = false;
    break;
  case 0x22: writeReg20Word2(&voice.cutoffCurrent_20, value); break;
  case 0x24:
    if (!ramp) break;
    writeReg20Word0(&voice.gainCurrent_24, value);
    voice.gain_gliding = false;
    break;
  case 0x26: writeReg20Word2(&voice.gainCurrent_24, value); break;

  // TODO cutoff/gain integrator
  case 0x28: case 0x2a: case 0x2c: case 0x2e: break;

  case 0x30:
    writeReg20Word0(&voice.cutoffTarget_30, value);
    if (ramp && active) {
      if (voice.cutoffTarget_30 & kTargetSnap) {
        voice.cutoffCurrent_20 = voice.cutoffTarget_30 & kTargetValue;
        voice.cutoff_gliding = false;
      } else {
        armGlide(int32_t(voice.cutoffCurrent_20),
                 int32_t(voice.cutoffTarget_30 & kTargetValue), glide_rate_,
                 &voice.cutoffStep_28, &voice.cutoff_gliding);
      }
    }
    break;
  case 0x32: writeReg20Word2(&voice.cutoffTarget_30, value); break;
  case 0x34:
    writeReg20Word0(&voice.gainTarget_34, value);
    if (ramp && active) {
      if (voice.gainTarget_34 & kTargetSnap) {
        voice.gainCurrent_24 = voice.gainTarget_34 & kTargetValue;
        voice.gain_gliding = false;
      } else {
        armGlide(int32_t(voice.gainCurrent_24),
                 int32_t(voice.gainTarget_34 & kTargetValue), glide_rate_,
                 &voice.gainStep_2c, &voice.gain_gliding);
      }
    }
    break;
  case 0x36: writeReg20Word2(&voice.gainTarget_34, value); break;

  default: break;
  }
}

uint32_t TVFEmulator::readPort24() const {
  const Voice& voice = voices_[port_voice_];
  switch (port_reg_) {
    case 0x00: return uint32_t(voice.svfStateBand_00) & 0xffffff;  // SVF BP integrator (die scale)
    case 0x04: return uint32_t(voice.svfStateHigh_04) & 0xffffff;  // companion (HP node)
    case 0x08: return voice.filterControl_08 & 0xffffff;
    case 0x0c: return uint32_t(voice.svfStateLow_0c) & 0xffffff;   // SVF LP integrator (die scale)
    case 0x20: return voice.cutoffCurrent_20 & 0xfffff;
    case 0x24: return voice.gainCurrent_24 & 0xfffff;
    case 0x28: return uint32_t(voice.cutoffStep_28) & 0xfffff;
    case 0x2c: return uint32_t(voice.gainStep_2c) & 0xfffff;
    case 0x30: return voice.cutoffTarget_30 & 0xfffff;
    case 0x34: return voice.gainTarget_34 & 0xfffff;
    case 0x38: case 0x3c: return 0x0fffff;
    default: return 0;
  }
}

uint8_t TVFEmulator::read(uint16_t offset) const {
  offset &= 0x7f;
  const uint32_t v24 = little_endian_bus_ ? readPort24() : port_latch_;
  const uint16_t lane = (offset & 2) ? uint16_t(v24 >> 16)
                                     : uint16_t(v24 & 0xffff);
  return little_endian_bus_ ? uint8_t((offset & 1) ? (lane >> 8) : lane)
                            : uint8_t((offset & 1) ? lane : (lane >> 8));
}

enum class MuxMode : uint8_t {
    LowPass = 0,
    HighPass = 1,
    BandPass = 2,
    Silent = 3,
};

enum class MuxStructure : uint8_t {
    Single = 0,
    Serial = 1,
    RingPre = 2,
    RingPost = 3,
};

[[noreturn]] static void invalidMuxSelector() {
    assert(false && "TVF mux selector outside its two-bit range");
    abort();
}

static inline int32_t filterModeMux(MuxMode mode, int32_t svfStateLow_00, int32_t svfStateHigh_04, int32_t svfStateBand_0c) {
    switch (mode) {
        case MuxMode::LowPass: return svfStateLow_00;
        case MuxMode::HighPass: return svfStateHigh_04;
        case MuxMode::BandPass: return svfStateBand_0c;
        case MuxMode::Silent: return 0;
    }
    invalidMuxSelector();
}

static inline int32_t preRingBypassMux(int32_t input, MuxStructure struct_, int32_t bypass) {
    switch (struct_) {
        case MuxStructure::Single: return input;
        case MuxStructure::Serial: return input;
        case MuxStructure::RingPre: return bypass ? input : 0;
        case MuxStructure::RingPost: return input;
    }
    invalidMuxSelector();
}

static inline int32_t postRingBypassMux(int32_t input, MuxStructure struct_, int32_t bypass) {
    switch (struct_) {
        case MuxStructure::Single: return input;
        case MuxStructure::Serial: return input;
        case MuxStructure::RingPre: return input;
        case MuxStructure::RingPost: return bypass ? input : 0;
    }
    invalidMuxSelector();
}

static inline int32_t mixMux(int32_t input, MuxStructure struct_) {
    switch (struct_) {
        case MuxStructure::Single: return 0;
        case MuxStructure::Serial: return input;
        case MuxStructure::RingPre: return 0;
        case MuxStructure::RingPost: return 0;
    }
    invalidMuxSelector();
}

static inline int32_t preTvaAMux(int32_t gainA, MuxStructure struct_) {
    switch (struct_) {
        case MuxStructure::Single: return mulUnity;
        case MuxStructure::Serial: return mulUnity;
        case MuxStructure::RingPre: return gainA;
        case MuxStructure::RingPost: return mulUnity;
    }
    invalidMuxSelector();
}

static inline int32_t postTvaAMux(int32_t gainA, MuxStructure struct_) {
    switch (struct_) {
        case MuxStructure::Single: return gainA;
        case MuxStructure::Serial: return 0;
        case MuxStructure::RingPre: return mulUnity >> 1; // unsure
        case MuxStructure::RingPost: return gainA;
    }
    invalidMuxSelector();
}

static inline int64_t& tvfAMux(MuxStructure struct_, int64_t& accA, int64_t& accB) {
    switch (struct_) {
        case MuxStructure::Single: return accA;
        case MuxStructure::Serial: return accB;
        case MuxStructure::RingPre: return accB;
        case MuxStructure::RingPost: return accA;
    }
    invalidMuxSelector();
}

template<size_t RenderVoiceCount>
void TVFEmulator::processFrame(const int32_t* const in_voices,
                               int32_t* const out_voices) {
  static_assert(RenderVoiceCount > 0, "TVF must render at least one voice");
  static_assert(RenderVoiceCount <= kVoiceSlots, "TVF render count exceeds its physical voice slots");
  static_assert((RenderVoiceCount & 1) == 0, "TVF renders paired voice contexts");
  const uint32_t active_mask = active_mask_;

  const bool ramp = rampEnabled();
  const bool tick = ++glide_tick_phase_ >= unsigned(2);
  if (tick) glide_tick_phase_ = 0;
  const uint8_t rate = glide_rate_;
  for (size_t index = 0; index < RenderVoiceCount; ++index) {
    Voice& voice = voices_[index];
    const bool active = (active_mask & (1u << index)) != 0;
    const bool rose = active && !(prev_active_mask_ & (1u << index));
    if (ramp && active) {
      const int32_t cutoff_tgt20 = int32_t(voice.cutoffTarget_30 & kTargetValue);
      const int32_t gain_tgt20 = int32_t(voice.gainTarget_34 & kTargetValue);
      if (rose) {
        if (voice.cutoffTarget_30 & kTargetSnap) {
          voice.cutoffCurrent_20 = uint32_t(cutoff_tgt20);
          voice.cutoff_gliding = false;
        } else {
          armGlide(int32_t(voice.cutoffCurrent_20), cutoff_tgt20, rate,
                   &voice.cutoffStep_28, &voice.cutoff_gliding);
        }
        if (voice.gainTarget_34 & kTargetSnap) {
          voice.gainCurrent_24 = uint32_t(gain_tgt20);
          voice.gain_gliding = false;
        } else {
          armGlide(int32_t(voice.gainCurrent_24), gain_tgt20, rate,
                   &voice.gainStep_2c, &voice.gain_gliding);
        }
      }
      if (!voice.cutoff_gliding && int32_t(voice.cutoffCurrent_20) != cutoff_tgt20)
        armGlide(int32_t(voice.cutoffCurrent_20), cutoff_tgt20, rate,
                 &voice.cutoffStep_28, &voice.cutoff_gliding);
      if (!voice.gain_gliding && int32_t(voice.gainCurrent_24) != gain_tgt20)
        armGlide(int32_t(voice.gainCurrent_24), gain_tgt20, rate,
                 &voice.gainStep_2c, &voice.gain_gliding);
      if (tick) {
        if (voice.cutoff_gliding) {
          int32_t cur = int32_t(voice.cutoffCurrent_20) + voice.cutoffStep_28;
          if ((voice.cutoffStep_28 > 0 && cur >= cutoff_tgt20) ||
              (voice.cutoffStep_28 < 0 && cur <= cutoff_tgt20)) {
            cur = cutoff_tgt20;
            voice.cutoff_gliding = false;
          }
          voice.cutoffCurrent_20 = uint32_t(cur) & 0xfffff;
        }
        if (voice.gain_gliding) {
          int32_t cur = int32_t(voice.gainCurrent_24) + voice.gainStep_2c;
          if ((voice.gainStep_2c > 0 && cur >= gain_tgt20) ||
              (voice.gainStep_2c < 0 && cur <= gain_tgt20)) {
            cur = gain_tgt20;
            voice.gain_gliding = false;
          }
          voice.gainCurrent_24 = uint32_t(cur) & 0xfffff;
        }
      }
    }
    out_voices[index] = 0;
  }
  prev_active_mask_ = active_mask;

  for (size_t index = 0; index < RenderVoiceCount; index += 2) {
    Voice& vA = voices_[index];
    Voice& vB = voices_[index + 1];
    
    const bool active_A = ((active_mask >> index) & 1) != 0;
    const bool active_B = ((active_mask >> (index + 1)) & 1) != 0;
    
    const uint32_t flags_A = vA.filterControl_08 & 0xff;
    const uint32_t flags_B = vB.filterControl_08 & 0xff;
    
    const int32_t input_A = in_voices[index];
    const int32_t input_B = in_voices[index + 1];
    const int32_t gain_A = int32_t(vA.gainCurrent_24 >> 4);
    const int32_t gain_B = int32_t(vB.gainCurrent_24 >> 4);
    const int32_t f_A = int32_t(vA.cutoffCurrent_20 >> 4);
    const int32_t f_B = int32_t(vB.cutoffCurrent_20 >> 4);
    const int32_t q_A = int32_t((vA.filterControl_08 >> 8) & 0x1fff) << 2;
    const int32_t q_B = int32_t((vB.filterControl_08 >> 8) & 0x1fff) << 2;
    int32_t &svfStateBand_00_A = vA.svfStateBand_00;
    int32_t &svfStateBand_00_B = vB.svfStateBand_00;
    int32_t &svfStateHigh_04_A = vA.svfStateHigh_04;
    int32_t &svfStateHigh_04_B = vB.svfStateHigh_04;
    int32_t &svfStateLow_0c_A = vA.svfStateLow_0c;
    int32_t &svfStateLow_0c_B = vB.svfStateLow_0c;
    const auto mode_A = static_cast<MuxMode>((vA.filterControl_08 & 0x06) >> 1);
    const auto mode_B = static_cast<MuxMode>((vB.filterControl_08 & 0x06) >> 1);
    const auto struct_A = static_cast<MuxStructure>((vA.filterControl_08 & 0x18) >> 3);
    // const uint32_t struct_B = (vB.filterControl_08 & 0x18) >> 3; // doesn't depend on this
    const uint32_t tvfRingBypass_A = (vA.filterControl_08 & 0x01) != 0;
    // const uint32_t tvfRingBypass_B = (vB.filterControl_08 & 0x01) != 0; // doesn't depend on this

    //
    // algorithm
    // ===================================

    // The accumulator (out_A/out_B) and the RAM A states run at DIE SCALE
    // (working << 6): sat24 on them clamps the working values at +-2^17 --
    // the hardware's resonance amplitude limiter. The multiplier's 18-bit
    // signal port takes WORKING values: bus inputs directly (input_A/B),
    // die-scale values as >>6 -- every >>6 below is that port.
    int64_t out_A = 0, out_B = 0;

    // 0: ??
    out_A = mac(0, input_A, preTvaAMux(gain_A, struct_A));  // 1: pre TVA A
    out_B = mac(mixMux(out_A, struct_A), input_B, mulUnity);  // 2: mix A + B
    out_B = mac(preRingBypassMux(out_B, struct_A, tvfRingBypass_A), (struct_A == MuxStructure::RingPre ? out_A >> 6 : 0), out_B >> 6);  // 3: pre RING

    if (!active_A) out_A = 0;
    if (!active_B) out_B = 0;

    // 4/5/6/7: TVF A
    svfStateLow_0c_A  = mac(svfStateLow_0c_A,                       svfStateBand_00_A >> 6, f_A);   // 4
    svfStateHigh_04_A = macSub(tvfAMux(struct_A, out_A, out_B),     svfStateLow_0c_A  >> 6, mulUnity);  // 5
    svfStateHigh_04_A = macSub(svfStateHigh_04_A,                   svfStateBand_00_A >> 6, q_A);   // 6
    svfStateBand_00_A = mac(svfStateBand_00_A,                      svfStateHigh_04_A >> 6, f_A);   // 7
    tvfAMux(struct_A, out_A, out_B) = filterModeMux(mode_A, svfStateLow_0c_A, svfStateHigh_04_A, svfStateBand_00_A);

    // 8/9/a/b: TVF B
    svfStateLow_0c_B  = mac(svfStateLow_0c_B,     svfStateBand_00_B >> 6, f_B);   // 8
    svfStateHigh_04_B = macSub(out_B,             svfStateLow_0c_B  >> 6, mulUnity);  // 9
    svfStateHigh_04_B = macSub(svfStateHigh_04_B, svfStateBand_00_B >> 6, q_B);  // a
    svfStateBand_00_B = mac(svfStateBand_00_B,    svfStateHigh_04_B >> 6, f_B);   // b
    out_B = filterModeMux(mode_B, svfStateLow_0c_B, svfStateHigh_04_B, svfStateBand_00_B);

    out_A = mac(0, out_A >> 6, postTvaAMux(gain_A, struct_A));  // c: post TVA A
    out_B = mac(postRingBypassMux(out_B, struct_A, tvfRingBypass_A), (struct_A == MuxStructure::RingPost ? out_A >> 6 : 0), out_B >> 6);  // d: post RING
    out_B = mac(0, out_B >> 6, gain_B);  // e: TVA B
    // f: ??

    // die scale -> the 18-bit output bus
    out_voices[index] = int32_t(out_A >> 6);
    out_voices[index + 1] = int32_t(out_B >> 6);
  }
}

template void TVFEmulator::processFrame<24>(const int32_t*, int32_t*);
template void TVFEmulator::processFrame<32>(const int32_t*, int32_t*);

}	// namespace tvfLib
