#include "cpu/sh2/ports7042.hpp"

namespace sh2 {

namespace {
constexpr u32 kPortMask[6] = {0x00FFFFFFu, 0x03FFu, 0xFFFFu, 0xFFFFFFFFu, 0xFFFFu, 0x00FFu};
}

void Ports7042::map(emu::IoMux& mux) {
  mux.assign(kPadr, 0x4C, this);   // H'8380-83CB: ports, PFC, ICSR / OCSR, IFCR
  mux.assign(kFlash, 4, this);
  mux.assign(kDtc, 16, this);
}

void Ports7042::reset() {
  dr_.fill(0);
  ior_.fill(0);
  in_.fill(0xFFFFFFFFu);
  ctl_.fill(0);
  flash_ = {0, 0, 0, 0};
  dtc_.fill(0);
}

// ---- 32-bit registers (ports A and D) ------------------------------------------

u32 Ports7042::read32(u32 a) {
  switch (a) {
    case kPadr: return read_dr(Port::A) & kPortMask[0];
    case kPaior: return ior_[0];
    case kPddr: return read_dr(Port::D);
    case kPdior: return ior_[3];
    default: return (u32(read16(a)) << 16) | read16(a + 2);
  }
}

void Ports7042::write32(u32 a, u32 v) {
  switch (a) {
    case kPadr: dr_[0] = v & kPortMask[0]; notify(Port::A); return;
    case kPaior: ior_[0] = v & kPortMask[0]; notify(Port::A); return;
    case kPddr: dr_[3] = v; notify(Port::D); return;
    case kPdior: ior_[3] = v; notify(Port::D); return;
    default:
      write16(a, u16(v >> 16));
      write16(a + 2, u16(v));
      return;
  }
}

// ---- 16-bit registers -----------------------------------------------------------

u16 Ports7042::read16(u32 a) {
  if ((a & ~7u) == kPadr || (a & ~7u) == kPddr) {  // halves of the 32-bit registers
    const u32 v = read32(a & ~3u);
    return u16((a & 2) ? v : v >> 16);
  }
  switch (a) {
    case kPbdr: return u16(read_dr(Port::B) & kPortMask[1]);
    case kPcdr: return u16(read_dr(Port::C));
    case kPbior: return u16(ior_[1]);
    case kPcior: return u16(ior_[2]);
    case kPedr: return u16(read_dr(Port::E));
    case kPfdr: return u16(in_[5] & kPortMask[5]);
    case kPeior: return u16(ior_[4]);
    default: break;
  }
  if (stored(a)) return u16((u16(ctl_[a - kPadr]) << 8) | ctl_[a - kPadr + 1]);
  return u16((u16(read8(a)) << 8) | read8(a + 1));
}

void Ports7042::write16(u32 a, u16 v) {
  if ((a & ~7u) == kPadr || (a & ~7u) == kPddr) {
    const u32 old = read32(a & ~3u);
    write32(a & ~3u, (a & 2) ? ((old & 0xFFFF0000u) | v) : ((old & 0x0000FFFFu) | (u32(v) << 16)));
    return;
  }
  switch (a) {
    case kPbdr: dr_[1] = v & kPortMask[1]; notify(Port::B); return;
    case kPcdr: dr_[2] = v; notify(Port::C); return;
    case kPbior: ior_[1] = v & kPortMask[1]; notify(Port::B); return;
    case kPcior: ior_[2] = v; notify(Port::C); return;
    case kPedr: dr_[4] = v; notify(Port::E); return;
    case kPfdr: return;  // input only
    case kPeior: ior_[4] = v; notify(Port::E); return;
    default: break;
  }
  if (stored(a)) {
    ctl_[a - kPadr] = u8(v >> 8);
    ctl_[a - kPadr + 1] = u8(v);
    return;
  }
  write8(a, u8(v >> 8));
  write8(a + 1, u8(v));
}

// ---- 8-bit registers and byte halves -----------------------------------------------

u8 Ports7042::read8(u32 a) {
  if (a >= kFlash && a < kFlash + 4) return flash_[a - kFlash];
  if (a >= kDtc && a < kDtc + 16) return dtc_[a - kDtc];
  if (!stored(a)) return 0xFF;
  const u16 w = read16(a & ~1u);
  return u8((a & 1) ? w : w >> 8);
}

void Ports7042::write8(u32 a, u8 v) {
  if (a >= kFlash && a < kFlash + 4) { flash_[a - kFlash] = v; return; }
  if (a >= kDtc && a < kDtc + 16) { dtc_[a - kDtc] = v; return; }
  if (!stored(a)) return;
  const u32 base = a & ~1u;
  const u16 old = read16(base);
  write16(base, (a & 1) ? u16((old & 0xFF00) | v) : u16((old & 0x00FF) | (u16(v) << 8)));
}

}  // namespace sh2
