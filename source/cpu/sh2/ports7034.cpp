#include "cpu/sh2/ports7034.hpp"

namespace sh2 {

void Ports7034::map(emu::IoMux& mux) {
  mux.assign(kUbc, 0x0A, this);
  mux.assign(kSbycr, 1, this);
  mux.assign(kPadr, 0x12, this);
  mux.assign(kCascr, 2, this);
  mux.assign(kTpc, 8, this);
}

void Ports7034::reset() {
  ubc_.fill(0);
  tpc_ = {0xF0, 0xFF, 0, 0, 0, 0, 0, 0};
  padr_ = pbdr_ = paior_ = pbior_ = 0;
  pacr1_ = 0x3302;
  pacr2_ = 0xFF95;
  pbcr1_ = pbcr2_ = 0;
  cascr_ = 0x5FFF;
  sbycr_ = 0x1F;
  cpu_.set_standby_request(false);
}

u16 Ports7034::read16(u32 addr) {
  if (addr >= kUbc && addr < kUbc + 0x0A) return ubc_[(addr - kUbc) >> 1];
  switch (addr) {
    case kPadr: return u16((in_a_ & ~paior_) | (padr_ & paior_));
    case kPbdr: return u16((in_b_ & ~pbior_) | (pbdr_ & pbior_));
    case kPaior: return paior_;
    case kPbior: return pbior_;
    case kPacr1: return pacr1_;
    case kPacr2: return pacr2_;
    case kPbcr1: return pbcr1_;
    case kPbcr2: return pbcr2_;
    case kPcdr: return in_c_;
    case kCascr: return cascr_;
    default:
      if (byte_register(addr)) return u16((u16(read8(addr)) << 8) | read8(addr + 1));
      return 0xFFFF;
  }
}

u8 Ports7034::read8(u32 addr) {
  if (addr == kSbycr) return sbycr_;
  if (addr >= kTpc && addr < kTpc + 8) return tpc_[addr - kTpc];
  if (!word_register(addr & ~1u)) return 0xFF;
  const u16 w = read16(addr & ~1u);
  return u8((addr & 1) ? w : w >> 8);
}

void Ports7034::write16(u32 addr, u16 value) {
  if (addr >= kUbc && addr < kUbc + 0x0A) { ubc_[(addr - kUbc) >> 1] = value; return; }
  switch (addr) {
    case kPadr: padr_ = value; if (hook_) hook_(0, output_a()); return;
    case kPbdr: pbdr_ = value; if (hook_) hook_(1, output_b()); return;
    case kPaior: paior_ = value; if (hook_) hook_(0, output_a()); return;
    case kPbior: pbior_ = value; if (hook_) hook_(1, output_b()); return;
    case kPacr1: pacr1_ = value; return;
    case kPacr2: pacr2_ = value; return;
    case kPbcr1: pbcr1_ = value; return;
    case kPbcr2: pbcr2_ = value; return;
    case kCascr: cascr_ = value; return;
    default:
      if (!byte_register(addr)) return;
      write8(addr, u8(value >> 8));
      write8(addr + 1, u8(value));
      return;
  }
}

void Ports7034::write8(u32 addr, u8 value) {
  if (addr == kSbycr) {
    sbycr_ = u8((value & 0xC0) | 0x1F);
    cpu_.set_standby_request((value & 0x80) != 0);
    return;
  }
  if (addr >= kTpc && addr < kTpc + 8) { tpc_[addr - kTpc] = value; return; }
  const u32 base = addr & ~1u;
  if (!word_register(base)) return;
  const u16 old = read16(base);
  write16(base, (addr & 1) ? u16((old & 0xFF00) | value) : u16((old & 0x00FF) | (u16(value) << 8)));
}

}  // namespace sh2
