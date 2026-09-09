#include "cpu/sh2/ports.hpp"

namespace sh2 {

namespace {

enum class Kind : u8 { Dr, Ior, Cr1, Cr2, Pfdr };

struct Reg {
  u32 addr;
  Port port;
  Kind kind;
};

// Every 16-bit register of sections 16 and 17 (appendix A addresses).
constexpr Reg kRegs[] = {
    {Ports::kPadrl, Port::A, Kind::Dr},   {Ports::kPaiorl, Port::A, Kind::Ior},
    {Ports::kPacrl1, Port::A, Kind::Cr1}, {Ports::kPacrl2, Port::A, Kind::Cr2},
    {Ports::kPbdr, Port::B, Kind::Dr},    {Ports::kPcdr, Port::C, Kind::Dr},
    {Ports::kPbior, Port::B, Kind::Ior},  {Ports::kPcior, Port::C, Kind::Ior},
    {Ports::kPbcr1, Port::B, Kind::Cr1},  {Ports::kPbcr2, Port::B, Kind::Cr2},
    {Ports::kPccr, Port::C, Kind::Cr2},   {Ports::kPddrl, Port::D, Kind::Dr},
    {Ports::kPdiorl, Port::D, Kind::Ior}, {Ports::kPdcrl, Port::D, Kind::Cr2},
    {Ports::kPedr, Port::E, Kind::Dr},    {Ports::kPfdr, Port::F, Kind::Pfdr},
    {Ports::kPeior, Port::E, Kind::Ior},  {Ports::kPecr1, Port::E, Kind::Cr1},
    {Ports::kPecr2, Port::E, Kind::Cr2},
};

const Reg* find_reg(u32 addr) {
  addr &= ~1u;
  for (const Reg& r : kRegs) if (r.addr == addr) return &r;
  return nullptr;
}

// Functions that only exist with an external bus / DMAC handshake: in
// single-chip mode their CR encodings leave the pin as general I/O
// ("PAx in single-chip mode", section 16.3).
bool bus_function(PinFunction f) {
  switch (f) {
    case PinFunction::Rd: case PinFunction::Wrh: case PinFunction::Wrl:
    case PinFunction::Cs0: case PinFunction::Cs1: case PinFunction::Cs2: case PinFunction::Cs3:
    case PinFunction::Address: case PinFunction::Data: case PinFunction::Rdwr:
    case PinFunction::Ras: case PinFunction::Cash: case PinFunction::Casl:
    case PinFunction::Wait: case PinFunction::Ah:
    case PinFunction::Dreq0: case PinFunction::Dreq1: case PinFunction::Dack0: case PinFunction::Dack1:
    case PinFunction::Drak0: case PinFunction::Drak1:
      return true;
    default:
      return false;
  }
}

// Two-bit MDn1/MDn0 field decoding.
PinFunction pick2(u16 reg, unsigned shift, PinFunction f1, PinFunction f2, PinFunction f3) {
  switch ((reg >> shift) & 3) {
    case 1: return f1;
    case 2: return f2;
    case 3: return f3;
    default: return PinFunction::Gpio;
  }
}
PinFunction pick1(u16 reg, unsigned bit, PinFunction f) {
  return (reg >> bit) & 1 ? f : PinFunction::Gpio;
}

}  // namespace

Ports::Ports(const ChipConfig& cfg) : cfg_(cfg) {
  const bool small = cfg_.model == ChipModel::SH7014;
  // Table 16.1 / register descriptions of 16.3: pins present per device and
  // the readable/writable bits of the control registers.
  P& a = p_[idx(Port::A)];
  a.pin_mask = small ? 0x83FF : 0xFFFF;          // SH7014: no PA14-PA10
  a.cr1_mask = small ? 0x400F : 0x555F;          // PACRL1: PA15MD, PA14-10MD (7016/17), PA9MD1-0, PA8MD1-0
  a.cr2_mask = 0xFD75;                           // PACRL2: bits 9, 7, 3, 1 reserved
  P& b = p_[idx(Port::B)];
  b.pin_mask = small ? 0x03FC : 0x03FF;          // SH7014: no PB1-PB0
  b.cr1_mask = 0x000F;                           // PBCR1: PB9MD1-0, PB8MD1-0
  b.cr2_mask = small ? 0xFFF0 : 0xFFF5;          // PBCR2: bits 3, 1 reserved; PB1MD/PB0MD 7016/17 only
  P& c = p_[idx(Port::C)];
  c.pin_mask = small ? 0x0000 : 0xFFFF;
  c.cr2_mask = 0xFFFF;                           // PCCR
  P& d = p_[idx(Port::D)];
  d.pin_mask = small ? 0x0000 : 0xFFFF;
  d.cr2_mask = 0xFFFF;                           // PDCRL
  P& e = p_[idx(Port::E)];
  e.pin_mask = 0xFFFF;
  e.cr1_mask = 0xF000;                           // PECR1: PE15MD1-0, PE14MD1-0
  e.cr2_mask = 0x55FF;                           // PECR2: bits 15, 13, 11, 9 reserved
  P& f = p_[idx(Port::F)];
  f.pin_mask = 0x00FF;                           // 8 input pins
  reset();
}

void Ports::map(emu::IoMux& mux) {
  for (const Reg& r : kRegs) {
    if (!has_port(r.port)) continue;  // port C/D registers are not decoded on the SH7014
    mux.assign(r.addr, 2, this);
  }
}

void Ports::reset() {
  for (P& p : p_) { p.dr = 0; p.ior = 0; p.cr1 = 0; p.cr2 = 0; }
  // PACRL1: PA15MD = 1 (CK output) in the extended modes, 0 in single-chip
  // mode (table 16.4 note 1).  Every other register resets to H'0000.
  if (cfg_.mode != 3) p_[idx(Port::A)].cr1 = 0x4000;
}

// ---------------------------------------------------------------------------
// Pin functions

PinFunction Ports::decode(Port port, unsigned pin) const {
  using F = PinFunction;
  const P& p = p_[idx(port)];
  switch (port) {
    case Port::A:
      switch (pin) {
        case 15: return pick1(p.cr1, 14, F::Ck);
        case 14: return pick1(p.cr1, 12, F::Rd);
        case 13: return pick1(p.cr1, 10, F::Wrh);
        case 12: return pick1(p.cr1, 8, F::Wrl);
        case 11: return pick1(p.cr1, 6, F::Cs1);
        case 10: return pick1(p.cr1, 4, F::Cs0);
        case 9: return pick2(p.cr1, 2, F::Tclkd, F::Irq3, F::Reserved);
        case 8: return pick2(p.cr1, 0, F::Tclkc, F::Irq2, F::Reserved);
        case 7: return pick2(p.cr2, 14, F::Tclkb, F::Cs3, F::Reserved);
        case 6: return pick2(p.cr2, 12, F::Tclka, F::Cs2, F::Reserved);
        case 5: return pick2(p.cr2, 10, F::Sck1, F::Dreq1, F::Irq1);
        case 4: return pick1(p.cr2, 8, F::Txd1);
        case 3: return pick1(p.cr2, 6, F::Rxd1);
        case 2: return pick2(p.cr2, 4, F::Sck0, F::Dreq0, F::Irq0);
        case 1: return pick1(p.cr2, 2, F::Txd0);
        default: return pick1(p.cr2, 0, F::Rxd0);
      }
    case Port::B:
      switch (pin) {
        case 9: return pick2(p.cr1, 2, F::Irq7, F::Address, F::Reserved);
        case 8: return pick2(p.cr1, 0, F::Irq6, F::Address, F::Wait);
        case 7: return pick2(p.cr2, 14, F::Reserved, F::Address, F::Reserved);
        case 6: return pick2(p.cr2, 12, F::Reserved, F::Address, F::Reserved);
        case 5: return pick2(p.cr2, 10, F::Irq3, F::Reserved, F::Rdwr);
        case 4: return pick2(p.cr2, 8, F::Irq2, F::Reserved, F::Cash);
        case 3: return pick2(p.cr2, 6, F::Irq1, F::Reserved, F::Casl);
        case 2: return pick2(p.cr2, 4, F::Irq0, F::Reserved, F::Ras);
        case 1: return pick1(p.cr2, 2, F::Address);
        default: return pick1(p.cr2, 0, F::Address);
      }
    case Port::C: return pick1(p.cr2, pin, F::Address);
    case Port::D: return pick1(p.cr2, pin, F::Data);
    case Port::E:
      switch (pin) {
        case 15: return pick2(p.cr1, 14, F::Reserved, F::Dack1, F::Reserved);
        case 14: return pick2(p.cr1, 12, F::Reserved, F::Dack0, F::Ah);
        case 7: return pick1(p.cr2, 14, F::Tioc2b);
        case 6: return pick1(p.cr2, 12, F::Tioc2a);
        case 5: return pick1(p.cr2, 10, F::Tioc1b);
        case 4: return pick1(p.cr2, 8, F::Tioc1a);
        case 3: return pick2(p.cr2, 6, F::Tioc0d, F::Drak1, F::Reserved);
        case 2: return pick2(p.cr2, 4, F::Tioc0c, F::Dreq1, F::Reserved);
        case 1: return pick2(p.cr2, 2, F::Tioc0b, F::Drak0, F::Reserved);
        case 0: return pick2(p.cr2, 0, F::Tioc0a, F::Dreq0, F::Reserved);
        default: return F::Gpio;  // PE13-PE8 have no second function
      }
    default:
      return F::Gpio;  // port F: general input (also the A/D analog input)
  }
}

PinFunction Ports::function(Port port, unsigned pin) const {
  pin &= 15;
  if (!((p_[idx(port)].pin_mask >> pin) & 1)) return PinFunction::None;
  // ROM-disabled extended modes (0, 1): the external bus lines are fixed
  // whatever the PFC says (tables 16.2/16.3, 17.1, 17.4, 17.7, 17.10).
  if (cfg_.mode <= 1) {
    switch (port) {
      case Port::A:
        switch (pin) {
          case 14: return PinFunction::Rd;
          case 13: return PinFunction::Wrh;
          case 12: return PinFunction::Wrl;
          case 11: return PinFunction::Cs1;
          case 10: return PinFunction::Cs0;
          default: break;
        }
        break;
      case Port::B:
        if (pin <= 1) return PinFunction::Address;  // A17, A16
        break;
      case Port::C: return PinFunction::Address;
      case Port::D: return PinFunction::Data;
      default: break;
    }
  }
  const PinFunction f = decode(port, pin);
  // Single-chip mode: no external bus, the bus / DMAC handshake encodings
  // leave the pin as general I/O.
  if (cfg_.mode == 3 && bus_function(f)) return PinFunction::Gpio;
  return f;
}

u16 Ports::gpio_mask(Port port) const {
  u16 m = 0;
  for (unsigned pin = 0; pin < 16; ++pin)
    if (function(port, pin) == PinFunction::Gpio) m |= u16(1u << pin);
  return m;
}

u16 Ports::pin_levels(Port port) const {
  const P& p = p_[idx(port)];
  const u16 out = u16(p.ior & gpio_mask(port));
  return u16(((p.dr & out) | (p.pins & ~out)) & p.pin_mask);
}

// ---------------------------------------------------------------------------
// Registers

u16 Ports::read_dr(Port port) const {
  // Tables 17.3/17.6/17.9/17.12/17.15: IOR = 1 -> latch, IOR = 0 -> pin
  // level, for the general-purpose and the "other" functions alike.
  const P& p = p_[idx(port)];
  return u16(((p.dr & p.ior) | (p.pins & ~p.ior)) & p.pin_mask);
}

u16 Ports::read16(u32 addr) {
  const Reg* r = find_reg(addr);
  if (!r || !has_port(r->port)) return 0xFFFF;
  const P& p = p_[idx(r->port)];
  switch (r->kind) {
    case Kind::Dr: return read_dr(r->port);
    case Kind::Ior: return p.ior;
    case Kind::Cr1: return p.cr1;
    case Kind::Cr2: return p.cr2;
    case Kind::Pfdr: return u16(p.pins & p.pin_mask);  // bits 15-8 reserved (read 0), PFDR in the low byte
  }
  return 0xFFFF;
}

void Ports::write16(u32 addr, u16 v) {
  const Reg* r = find_reg(addr);
  if (!r || !has_port(r->port)) return;
  P& p = p_[idx(r->port)];
  switch (r->kind) {
    case Kind::Dr:
      p.dr = u16(v & p.pin_mask);  // bits without a pin: "write value should always be 0"
      notify(r->port);
      break;
    case Kind::Ior:
      p.ior = u16(v & p.pin_mask);
      notify(r->port);
      break;
    case Kind::Cr1: p.cr1 = u16(v & p.cr1_mask); break;
    case Kind::Cr2: p.cr2 = u16(v & p.cr2_mask); break;
    case Kind::Pfdr: break;  // input only: writes ignored (17.7.2)
  }
}

u8 Ports::read8(u32 addr) {
  const u16 v = read16(addr & ~1u);
  return (addr & 1) ? u8(v) : u8(v >> 8);
}

void Ports::write8(u32 addr, u8 v) {
  // Byte write = replace one half of the stored register (not of the read
  // value, which mixes pin levels into a DR).
  const Reg* r = find_reg(addr);
  if (!r || !has_port(r->port)) return;
  const P& p = p_[idx(r->port)];
  u16 cur = 0;
  switch (r->kind) {
    case Kind::Dr: cur = p.dr; break;
    case Kind::Ior: cur = p.ior; break;
    case Kind::Cr1: cur = p.cr1; break;
    case Kind::Cr2: cur = p.cr2; break;
    case Kind::Pfdr: return;
  }
  const u16 nv = (addr & 1) ? u16((cur & 0xFF00) | v) : u16((cur & 0x00FF) | (u16(v) << 8));
  write16(addr & ~1u, nv);
}

// ---------------------------------------------------------------------------
// FlashRegs

FlashRegs::FlashRegs(const ChipConfig& cfg) : cfg_(cfg) { reset(); }

void FlashRegs::map(emu::IoMux& mux) {
  if (!present()) return;  // SH7014/16: the addresses stay unmapped (read H'FF)
  mux.assign(kFlmcr1, 3, this);
  mux.assign(kRamer, 2, this);
}

void FlashRegs::reset() {
  // Reset / standby initialise FLMCR1 (bits 6-0), FLMCR2 (FLER), EBR1 and
  // RAMER (18.5, table 18.8).  FWE keeps following the pin.
  flmcr1_ = 0;
  fler_ = 0;
  ebr1_ = 0;
  ramer_ = 0;
}

void FlashRegs::set_fwe(bool high) {
  fwe_ = high;
  if (!high) {  // FWP low: hardware protection, FLMCR1 and EBR1 initialised (table 18.8)
    flmcr1_ = 0;
    ebr1_ = 0;
  }
}

u8 FlashRegs::flmcr1() const {
  if (!flash_enabled()) return 0;  // table 18.3 note 1
  return u8((fwe_ ? kFwe : 0) | flmcr1_);
}

u8 FlashRegs::read8(u32 addr) {
  switch (addr) {
    case kFlmcr1: return flmcr1();
    case kFlmcr2: return flmcr2();
    case kEbr1: return ebr1();
    case kRamer: return u8(ramer_ >> 8);      // reserved, reads 0
    case kRamer + 1: return u8(ramer_);
    default: return 0xFF;
  }
}

void FlashRegs::write8(u32 addr, u8 v) {
  switch (addr) {
    case kFlmcr1: {
      if (!flash_enabled()) return;  // writes invalid with the flash disabled (table 18.3 note 1)
      // 18.5.1: each bit is writable only while its setting condition holds,
      // evaluated on the value before the write (so setting SWE together
      // with a lower bit only takes SWE, as the "do not set at the same
      // time" notes require).  FWE (bit 7) is the FWP pin and ignores writes.
      const u8 old = flmcr1_;
      u8 nv = old;
      auto pass = [&](u8 bit, bool cond) { if (cond) nv = u8((nv & ~bit) | (v & bit)); };
      const bool swe = fwe_ && (old & kSwe);
      pass(kSwe, fwe_);
      pass(u8(kEsu | kPsu | kEv | kPv), swe);
      pass(kE, swe && (old & kEsu));
      pass(kP, swe && (old & kPsu));
      flmcr1_ = u8(nv & 0x7F);
      // Programming / erasing itself is not modelled: P and E are plain bits.
      if (!(flmcr1_ & kSwe)) ebr1_ = 0;  // EBR1 is held at H'00 while SWE = 0 (18.5.3)
      break;
    }
    case kFlmcr2:
      break;  // read-only
    case kEbr1:
      if (!flash_enabled() || !fwe_ || !(flmcr1_ & kSwe)) return;  // 18.5.3
      ebr1_ = v;
      break;
    case kRamer:
      break;  // bits 15-8 reserved
    case kRamer + 1:
      ramer_ = u16(v & (kRams | kRamMask));  // bits 7-3 reserved, read 0
      break;
    default:
      break;
  }
}

u16 FlashRegs::read16(u32 addr) {
  if (addr == kRamer) return ramer_;
  return Device::read16(addr);  // FLMCR1/2, EBR1: byte registers (word access undefined on the chip)
}

void FlashRegs::write16(u32 addr, u16 v) {
  if (addr == kRamer) { ramer_ = u16(v & (kRams | kRamMask)); return; }
  Device::write16(addr, v);
}

}  // namespace sh2
