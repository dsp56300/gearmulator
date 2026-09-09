// SH7034 pin function controller and I/O ports (SH7032/34 hardware manual
// sections 15, 16), with the register-only modules of the same address block:
// the user break controller (6), the timing pattern controller (11) and the
// standby control register (19).
//
//   H'5FFFF90-99 BARH BARL BAMRH BAMRL BBR   (UBC: stored, no break condition is checked)
//   H'5FFFFBC    SBYCR                        (SBY forwards the standby request to the CPU)
//   H'5FFFFC0    PADR  H'5FFFFC2 PBDR         port A / B data: outputs from PADR / PBDR
//   H'5FFFFC4    PAIOR H'5FFFFC6 PBIOR        direction (1 = output)
//   H'5FFFFC8-CF PACR1 PACR2 PBCR1 PBCR2     pin functions: stored, GPIO assumed
//   H'5FFFFD0    PCDR                          port C input (8 bits)
//   H'5FFFFEE    CASCR
//   H'5FFFFF0-F7 TPMR TPCR NDERA NDERB NDRB NDRA (stored)
// A data register reads the pin level on input bits and the register on
// output bits; writes call the output hook with the output bits.
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/cpu.hpp"

namespace sh2 {

class Ports7034 final : public Device {
 public:
  static constexpr u32 kUbc = 0x05FFFF90u, kSbycr = 0x05FFFFBCu, kPadr = 0x05FFFFC0u, kPbdr = 0x05FFFFC2u,
                       kPaior = 0x05FFFFC4u, kPbior = 0x05FFFFC6u, kPacr1 = 0x05FFFFC8u, kPacr2 = 0x05FFFFCAu,
                       kPbcr1 = 0x05FFFFCCu, kPbcr2 = 0x05FFFFCEu, kPcdr = 0x05FFFFD0u, kCascr = 0x05FFFFEEu,
                       kTpc = 0x05FFFFF0u;
  using OutputHook = std::function<void(unsigned port, u16 value)>;  // port 0 = A, 1 = B

  explicit Ports7034(Cpu& cpu) : cpu_(cpu) { reset(); }

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- host side -----------------------------------------------------------
  void set_input_a(u16 v) { in_a_ = v; }
  void set_input_b(u16 v) { in_b_ = v; }
  void set_input_c(u8 v) { in_c_ = v; }
  void set_output_hook(OutputHook h) { hook_ = std::move(h); }
  u16 output_a() const { return u16(padr_ & paior_); }
  u16 output_b() const { return u16(pbdr_ & pbior_); }
  u16 paior() const { return paior_; }
  u16 pbior() const { return pbior_; }
  u8 sbycr() const { return sbycr_; }

 private:
  static bool word_register(u32 a) { return (a >= kUbc && a < kUbc + 0x0A) || (a >= kPadr && a <= kPcdr) || a == kCascr; }
  static bool byte_register(u32 a) { return a == kSbycr || (a >= kTpc && a < kTpc + 8); }

  Cpu& cpu_;
  OutputHook hook_;
  std::array<u16, 5> ubc_{};
  std::array<u8, 8> tpc_{};
  u16 padr_ = 0, pbdr_ = 0, paior_ = 0, pbior_ = 0, pacr1_ = 0x3302, pacr2_ = 0xFF95, pbcr1_ = 0, pbcr2_ = 0;
  u16 cascr_ = 0x5FFF, in_a_ = 0, in_b_ = 0;
  u8 in_c_ = 0, sbycr_ = 0x1F;
};

}  // namespace sh2
