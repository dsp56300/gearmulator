// SH7042 pin function controller and I/O ports (SH7040 hardware manual
// sections 17, 18), with the register-only modules of the same block: the
// MTU output-enable / interrupt control (ICSR, OCSR), the IRQ function
// control (IFCR), the DTC (DTEA-E, DTCSR, DTBR) and the flash control
// registers.  Register storage with host pin inputs and an output hook; the
// pin-function decoding of the SH7014 Ports is not reproduced.
//
//   H'FFFF8380 PADR (24 bits, longword)  +4 PAIOR   +8 PACRH  +C PACRL1  +E PACRL2
//   H'FFFF8390 PBDR   +2 PCDR   +4 PBIOR  +6 PCIOR  +8 PBCR1  +A PBCR2  +C PCCR
//   H'FFFF83A0 PDDR (32 bits)  +4 PDIOR  +8 PDCRH1  +A PDCRH2  +C PDCRL
//   H'FFFF83B0 PEDR   +2 PFDR (input)  +4 PEIOR  +8 PECR1  +A PECR2
//   H'FFFF83C0 ICSR   +2 OCSR   H'FFFF83C8 IFCR
//   H'FFFF8580 FLMCR1 FLMCR2 EBR1 EBR2    H'FFFF8700 DTEA-DTEE, DTCSR, DTBR
#pragma once
#include <array>
#include <functional>

#include "common/iomux.hpp"
#include "cpu/sh2/bus.hpp"

namespace sh2 {

class Ports7042 final : public Device {
 public:
  enum class Port : u8 { A, B, C, D, E, F };
  static constexpr u32 kPadr = 0xFFFF8380u, kPaior = 0xFFFF8384u, kPbdr = 0xFFFF8390u, kPcdr = 0xFFFF8392u,
                       kPbior = 0xFFFF8394u, kPcior = 0xFFFF8396u, kPddr = 0xFFFF83A0u, kPdior = 0xFFFF83A4u,
                       kPedr = 0xFFFF83B0u, kPfdr = 0xFFFF83B2u, kPeior = 0xFFFF83B4u, kIcsr = 0xFFFF83C0u,
                       kIfcr = 0xFFFF83C8u, kFlash = 0xFFFF8580u, kDtc = 0xFFFF8700u;
  using OutputHook = std::function<void(Port port, u32 value, u32 direction)>;

  Ports7042() { reset(); }

  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;
  u32 read32(u32 addr) override;
  void write32(u32 addr, u32 value) override;

  // --- host side -----------------------------------------------------------
  void set_input(Port p, u32 levels) { in_[unsigned(p)] = levels; }
  void set_output_hook(OutputHook h) { hook_ = std::move(h); }
  u32 dr(Port p) const { return dr_[unsigned(p)]; }
  u32 ior(Port p) const { return ior_[unsigned(p)]; }
  u32 output(Port p) const { return dr_[unsigned(p)] & ior_[unsigned(p)]; }

 private:
  // The 0x80-byte control block H'FFFF8380-83FF as bytes, for the registers
  // that are only stored (PFC control, ICSR / OCSR / IFCR).
  bool stored(u32 a) const { return a >= kPadr && a < kPadr + 0x80; }
  u32 read_dr(Port p) const { return (in_[unsigned(p)] & ~ior_[unsigned(p)]) | (dr_[unsigned(p)] & ior_[unsigned(p)]); }
  void notify(Port p) { if (hook_) hook_(p, output(p), ior_[unsigned(p)]); }

  OutputHook hook_;
  std::array<u32, 6> dr_{}, ior_{}, in_{};
  std::array<u8, 0x80> ctl_{};
  std::array<u8, 4> flash_{};
  std::array<u8, 16> dtc_{};
};

}  // namespace sh2
