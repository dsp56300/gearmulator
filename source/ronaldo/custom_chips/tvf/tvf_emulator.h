#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tvfLib
{

class TVFEmulator {
public:
  static constexpr size_t kVoiceSlots = 32;

  explicit TVFEmulator(bool littleEndianBus = false);

  void reset();

  void write(uint16_t offset, uint8_t value);
  uint8_t read(uint16_t offset) const;

  template<size_t RenderVoiceCount>
  void processFrame(const int32_t* in_voices, int32_t* out_voices);

private:
  struct Voice {
    // RAM A (24 bit words, 4 per voice)
    int32_t  svfStateBand_00;   // SVF BP integrator, signed
    int32_t  svfStateHigh_04;   // SVF HP companion state, signed
    uint32_t filterControl_08;  // [20:8] damping Q12, [7:0] flags byte
    int32_t  svfStateLow_0c;    // SVF LP integrator, signed

    // RAM B (20 bit words, 6 per voice)
    uint32_t cutoffCurrent_20;
    uint32_t gainCurrent_24;
    int32_t  cutoffStep_28;     // signed glide distance per tick
    int32_t  gainStep_2c;
    uint32_t cutoffTarget_30;   // [19] snap flag, [18:4] Q14 destination
    uint32_t gainTarget_34;     // [19] snap flag, [18:4] Q14 destination

    bool cutoff_gliding;
    bool gain_gliding;
  };

  enum FilterMode : uint32_t { kModeLP = 0, kModeHP = 1, kModeBP = 2 };
  enum Structure : uint32_t {
    kStructSingle   = 0,  // A -> TVF A -> TVA A
    kStructSerial   = 1,  // A + B -> TVF A -> TVF B -> TVA B
    kStructRingPre  = 2,  // A -> TVA A -> RING(B) -> TVF A -> TVF B -> TVA B
    kStructRingPost = 3,  // A -> TVF A -> TVA A -> RING(B -> TVF B) -> TVA B
  };
  
  static constexpr uint32_t kTargetSnap  = 1u << 19;
  static constexpr uint32_t kTargetValue = 0x7ffff;

  void write16(uint16_t offset, uint16_t value);
  uint32_t readPort24() const;

  bool rampEnabled() const;
  static int32_t glideStep(int32_t dist20, uint8_t rate);
  static void armGlide(int32_t cur20, int32_t tgt20, uint8_t rate,
                       int32_t* step20, bool* gliding);
  int32_t filterSample(Voice& voice, int32_t input);

  uint8_t global_[0x80];
  Voice voices_[kVoiceSlots];

  uint32_t active_mask_;     // 0x10 (hi) / 0x12 (lo) voice bitmap
  uint16_t ramp_enable_;   // 0x14
  uint8_t  glide_rate_;      // 0x18, low byte only (high byte + bits[7:5] inert)

  // Previous frame's active mask: a bit's rising edge delivers the voice's
  // pending target words to the ramp (SET snaps, CLEAR glides).
  uint32_t prev_active_mask_;
  // Frame parity: glide steps apply every 2nd frame (fs/2).
  unsigned glide_tick_phase_;
  uint8_t selected_voice_;
  // Read-port source: any word write to a selector offset 0x40..0x7e re-points
  // the port at (per-voice register, context) -- last write wins.
  uint8_t port_reg_;
  uint8_t port_voice_;
  uint32_t port_latch_;
  bool little_endian_bus_;
};

}	// namespace tvfLib
