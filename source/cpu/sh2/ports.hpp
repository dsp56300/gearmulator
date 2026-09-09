// SH7014/16/17 pin function controller (hardware manual section 16), I/O
// ports (section 17) and the F-ZTAT flash-memory control registers of the
// SH7017 (section 18.4/18.5).
//
// Ports A-F share one Device.  Every register is 16 bits wide (table 8.5) and
// is accessed as a unit through read16/write16; byte accesses read or write
// one half of the same value.  Per port there are up to three registers:
//   DR   data register: the output latch.  A read returns the latch for pins
//        whose IOR bit is 1 and the pin level (host-supplied) for the others
//        (tables 17.3, 17.6, 17.9, 17.12, 17.15), whatever function the pin
//        is currently assigned to.
//   IOR  I/O register: 1 = output.  Meaningful for the general-purpose
//        function (and SCK / TIOC pins, whose direction it also selects).
//   CR   control register(s): the multiplexed function of each pin.  The
//        controller only stores them; function() decodes them, together with
//        the operating mode, so that the board and the other modules can ask
//        which signal a pin carries.
//
// Model differences (tables 16.1-16.3): the SH7014 has no port C or D and
// lacks PA14-PA10 and PB1-PB0 (their DR/IOR/CR bits read 0, their write
// value is ignored, and the port C/D registers are not decoded at all, so
// they read as unmapped register-field bytes, H'FF).  In the ROM-disabled
// modes 0/1 of the SH7016/17 the PA14-PA10 pins are the bus-control lines,
// PB1-PB0 are A17-A16, port C is A15-A0 and port D is D15-D0 regardless of
// the PFC settings; in single-chip mode (3) the bus / DMAC-handshake
// function encodings fall back to general I/O (the "PAx in single-chip
// mode" notes of section 16.3).
//
// Port F is an 8-bit input-only port sharing its pins with the A/D inputs;
// PFDR (the low byte of the word at H'FFFF83B2) reads the pin levels and
// ignores writes.  The "1 is read while the A/D converter samples the pin"
// behaviour (table 17.17) is not modelled.
//
// Host side: set_pins() supplies the level of every pin of a port,
// pin_levels() gives what the outside world sees (latch where the pin is a
// general-purpose output, host level elsewhere) and the write hook fires
// after every DR / IOR write with the new values.
#pragma once
#include <functional>

#include "common/iomux.hpp"
#include "cpu/sh2/bus.hpp"
#include "cpu/sh2/chip.hpp"

namespace sh2 {

enum class Port : u8 { A, B, C, D, E, F, kCount };

// Signal carried by a multiplexed pin (tables 16.1 and 16.2/16.3).  Address
// and data lines are reported generically: port C pin n is An, PB0/PB1 are
// A16/A17, PB6-PB9 are A18-A21 and port D pin n is Dn.
enum class PinFunction : u8 {
  None,      // no such pin on this device
  Gpio,      // general-purpose input/output (port F: general input / analog input)
  Reserved,  // reserved CR encoding
  // CPG / BSC
  Ck, Rd, Wrh, Wrl, Cs0, Cs1, Cs2, Cs3, Address, Data, Rdwr, Ras, Cash, Casl, Wait, Ah,
  // INTC
  Irq0, Irq1, Irq2, Irq3, Irq6, Irq7,
  // MTU
  Tclka, Tclkb, Tclkc, Tclkd, Tioc0a, Tioc0b, Tioc0c, Tioc0d, Tioc1a, Tioc1b, Tioc2a, Tioc2b,
  // SCI
  Sck0, Txd0, Rxd0, Sck1, Txd1, Rxd1,
  // DMAC
  Dreq0, Dreq1, Dack0, Dack1, Drak0, Drak1,
};

class Ports final : public Device {
 public:
  // Register addresses (appendix A).
  static constexpr u32 kPadrl = 0xFFFF8382u, kPaiorl = 0xFFFF8386u, kPacrl1 = 0xFFFF838Cu, kPacrl2 = 0xFFFF838Eu;
  static constexpr u32 kPbdr = 0xFFFF8390u, kPcdr = 0xFFFF8392u, kPbior = 0xFFFF8394u, kPcior = 0xFFFF8396u;
  static constexpr u32 kPbcr1 = 0xFFFF8398u, kPbcr2 = 0xFFFF839Au, kPccr = 0xFFFF839Cu;
  static constexpr u32 kPddrl = 0xFFFF83A2u, kPdiorl = 0xFFFF83A6u, kPdcrl = 0xFFFF83ACu;
  static constexpr u32 kPedr = 0xFFFF83B0u, kPfdr = 0xFFFF83B2u, kPeior = 0xFFFF83B4u;
  static constexpr u32 kPecr1 = 0xFFFF83B8u, kPecr2 = 0xFFFF83BAu;

  // Called after a DR or IOR write with the new latch and direction.
  using WriteHook = std::function<void(Port port, u16 dr, u16 ior)>;

  explicit Ports(const ChipConfig& cfg);

  void map(emu::IoMux& mux);
  void reset();  // external power-on reset (WDT resets do not touch these registers)

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- host side -----------------------------------------------------------
  void set_pins(Port port, u16 levels) { p_[idx(port)].pins = levels; }
  void set_write_hook(WriteHook h) { hook_ = std::move(h); }
  // Levels seen outside the chip: the latch where the pin is a general-purpose
  // output, the host level elsewhere (inputs and pins driven by other modules).
  u16 pin_levels(Port port) const;
  u16 dr(Port port) const { return p_[idx(port)].dr; }
  u16 ior(Port port) const { return p_[idx(port)].ior; }
  // Raw control registers: PACRL1/PBCR1/PECR1 = cr1, PACRL2/PBCR2/PCCR/PDCRL/PECR2 = cr2.
  u16 cr1(Port port) const { return p_[idx(port)].cr1; }
  u16 cr2(Port port) const { return p_[idx(port)].cr2; }
  // Pins that exist on this device (bit n = pin n).
  u16 pin_mask(Port port) const { return p_[idx(port)].pin_mask; }
  // Function currently carried by pin `pin` (0-15) of `port`, after the
  // operating-mode overrides of tables 16.2/16.3.
  PinFunction function(Port port, unsigned pin) const;
  // Mask of the pins of a port currently assigned to general-purpose I/O.
  u16 gpio_mask(Port port) const;

 private:
  struct P {
    u16 dr = 0, ior = 0, cr1 = 0, cr2 = 0;
    u16 pins = 0xFFFF;   // host-supplied levels
    u16 pin_mask = 0;    // pins present on this device
    u16 cr1_mask = 0, cr2_mask = 0;  // writable / readable CR bits
  };
  static unsigned idx(Port p) { return unsigned(p) < unsigned(Port::kCount) ? unsigned(p) : 0; }
  bool has_port(Port p) const { return p_[idx(p)].pin_mask != 0; }
  u16 read_dr(Port port) const;
  PinFunction decode(Port port, unsigned pin) const;
  void notify(Port port) { if (hook_) hook_(port, p_[idx(port)].dr, p_[idx(port)].ior); }

  const ChipConfig& cfg_;
  P p_[unsigned(Port::kCount)];
  WriteHook hook_;
};

// Flash-memory control registers of the SH7017 (F-ZTAT, section 18):
//   FLMCR1 H'FFFF8580  FWE SWE ESU PSU EV PV E P      (8-bit)
//   FLMCR2 H'FFFF8581  FLER -------                   (8-bit, read-only)
//   EBR1   H'FFFF8582  EB7-EB0 erase block select     (8-bit)
//   RAMER  H'FFFF8628  ----- RAMS RAM1 RAM0           (16-bit)
// Only the register file is modelled: the bit-write rules of 18.5.1 (SWE
// needs FWE; ESU/PSU/EV/PV need FWE and SWE; E needs ESU too, P needs PSU
// too), FWE following the FWP pin, the FWP-low / reset initialisation of
// FLMCR1 and EBR1 (table 18.8), EBR1 forced to 0 while SWE = 0, and the
// "flash disabled" behaviour of modes 0/1 (FLMCR1/2 and EBR1 read H'00,
// writes ignored; table 18.3 notes 1-2).  Setting P or E does NOT program or
// erase anything: the on-chip ROM image never changes, the programming /
// erasing waits and the error protection (FLER, 18.8.3) are not modelled, so
// FLER always reads 0.  RAMER is stored and exposed (ram_emulation(),
// ram_overlay_base()) so the Machine can overlay the 1 KB RAM area
// H'FFFFF800-H'FFFFFBFF onto the selected 1 KB flash block (table 18.5).
// On the SH7014/16 map() decodes nothing, so the addresses read as unmapped
// register-field bytes (H'FF).
class FlashRegs final : public Device {
 public:
  static constexpr u32 kFlmcr1 = 0xFFFF8580u, kFlmcr2 = 0xFFFF8581u, kEbr1 = 0xFFFF8582u, kRamer = 0xFFFF8628u;
  // FLMCR1 bits.
  static constexpr u8 kFwe = 0x80, kSwe = 0x40, kEsu = 0x20, kPsu = 0x10, kEv = 0x08, kPv = 0x04, kE = 0x02, kP = 0x01;
  // RAMER bits.
  static constexpr u16 kRams = 0x0004, kRamMask = 0x0003;
  static constexpr u32 kRamAreaBase = 0xFFFFF800u, kRamAreaSize = 0x400;  // RAM area used for emulation (table 18.5)
  static constexpr u32 kFlashBlockEb4 = 0x0001F000u;                      // EB4; EB5-EB7 follow at 1 KB steps

  explicit FlashRegs(const ChipConfig& cfg);

  bool present() const { return cfg_.model == ChipModel::SH7017; }
  void map(emu::IoMux& mux);
  void reset();

  u8 read8(u32 addr) override;
  void write8(u32 addr, u8 value) override;
  u16 read16(u32 addr) override;
  void write16(u32 addr, u16 value) override;

  // --- host side -----------------------------------------------------------
  // FWP pin level (high = programming enabled).  Low initialises FLMCR1
  // (except FWE) and EBR1 (table 18.8).
  void set_fwe(bool high);
  bool fwe() const { return fwe_; }
  u8 flmcr1() const;
  u8 flmcr2() const { return flash_enabled() ? fler_ : u8(0); }
  u8 ebr1() const { return flash_enabled() ? ebr1_ : u8(0); }
  u16 ramer() const { return ramer_; }
  // RAM emulation (RAMS): the 1 KB RAM area is overlaid on flash block EB4+ram_block().
  bool ram_emulation() const { return (ramer_ & kRams) != 0; }
  unsigned ram_block() const { return ramer_ & kRamMask; }  // 0-3 = EB4-EB7
  u32 ram_overlay_base() const { return kFlashBlockEb4 + ram_block() * kRamAreaSize; }

 private:
  bool flash_enabled() const { return present() && cfg_.rom_enabled(); }

  const ChipConfig& cfg_;
  bool fwe_ = false;
  u8 flmcr1_ = 0;  // bits 6-0 (FWE comes from the pin)
  u8 fler_ = 0;
  u8 ebr1_ = 0;
  u16 ramer_ = 0;
};

}  // namespace sh2
