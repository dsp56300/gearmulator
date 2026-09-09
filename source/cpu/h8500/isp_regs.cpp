#include "cpu/h8500/isp_regs.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {
constexpr u32 kFlagBase = 0xFEB0, kIpr = 0xFF18, kIcsr = 0xFF19, kFedge = 0xFF28, kRedge = 0xFF29;
}

IspRegs::IspRegs(Intc& intc) : intc_(intc) {}

void IspRegs::map(emu::IoMux& mux) {
  mux.assign(kFlagBase, 16, this);
  mux.assign(kIpr, 2, this);
  mux.assign(kFedge, 2, this);
}

void IspRegs::reset() {
  isf_ = iof_ = icf_ = ief_ = ioie_ = cle_ = 0;
  iof0_ = egf_ = ever_ = 0;
  ipr_ = 0;
  icsr_ = 0x20;
  fedge_ = redge_ = 0;
  update_delivery();
}

void IspRegs::update_delivery() {
  const u16 deliver = u16(isf_ & ief_);
  for (unsigned n = 0; n < 16; ++n) intc_.set_request(isf_src(n), (deliver >> n) & 1);
}

void IspRegs::raise_isf(unsigned n, bool level) {
  n &= 15;
  if (level) isf_ |= u16(1u << n);
  else isf_ &= u16(~(1u << n));
  update_delivery();
}

void IspRegs::set_icf(unsigned n, bool level) {
  n &= 15;
  if (level) icf_ |= u16(1u << n);
  else icf_ &= u16(~(1u << n));
}

u8 IspRegs::read8(u32 addr) {
  switch (addr) {
    case kFlagBase + 0x0: return u8(isf_ >> 8);
    case kFlagBase + 0x1: return u8(isf_);
    case kFlagBase + 0x2: return u8(iof_ >> 8);
    case kFlagBase + 0x3: return u8(iof_);
    case kFlagBase + 0x4: return iof0_;
    case kFlagBase + 0x5: return egf_;
    case kFlagBase + 0x6: {
      const u8 v = u8(icf_ >> 8);
      icf_ &= u16(~(u16(cle_ >> 8) << 8));  // CLEH: read clears
      return v;
    }
    case kFlagBase + 0x7: {
      const u8 v = u8(icf_);
      icf_ &= u16(~(cle_ & 0xFF));
      return v;
    }
    case kFlagBase + 0x8: return u8(ief_ >> 8);
    case kFlagBase + 0x9: return u8(ief_);
    case kFlagBase + 0xA: return u8(ioie_ >> 8);
    case kFlagBase + 0xB: return u8(ioie_);
    case kFlagBase + 0xC: return u8(cle_ >> 8);
    case kFlagBase + 0xD: return u8(cle_);
    case kFlagBase + 0xF: return ever_;
    case kIpr: return ipr_;
    case kIcsr: return icsr_;
    case kFedge: return fedge_;
    case kRedge: return redge_;
    default: return 0xFF;
  }
}

void IspRegs::write8(u32 addr, u8 v) {
  switch (addr) {
    case kFlagBase + 0x0:
    case kFlagBase + 0x1: {
      // Handlers acknowledge by writing their bit back as 0; the firmware also
      // pends ISFs deliberately by writing 1, so the write is taken as is.
      const unsigned shift = addr == kFlagBase ? 8 : 0;
      const u16 prev = isf_;
      isf_ = u16((isf_ & ~(0xFFu << shift)) | (u16(v) << shift));
      update_delivery();
      const u16 cleared = u16(prev & ~isf_);
      for (unsigned n = 0; n < 16; ++n)
        if ((cleared >> n) & 1 && isf_clear_hook_) isf_clear_hook_(n);
      return;
    }
    case kFlagBase + 0x6: icf_ = u16((icf_ & 0x00FF) | (u16(v) << 8)); return;
    case kFlagBase + 0x7: icf_ = u16((icf_ & 0xFF00) | v); return;
    case kFlagBase + 0x8: ief_ = u16((ief_ & 0x00FF) | (u16(v) << 8)); update_delivery(); return;
    case kFlagBase + 0x9: ief_ = u16((ief_ & 0xFF00) | v); update_delivery(); return;
    case kFlagBase + 0xA: ioie_ = u16((ioie_ & 0x00FF) | (u16(v) << 8)); return;
    case kFlagBase + 0xB: ioie_ = u16((ioie_ & 0xFF00) | v); return;
    case kFlagBase + 0xC: cle_ = u16((cle_ & 0x00FF) | (u16(v) << 8)); return;
    case kFlagBase + 0xD: cle_ = u16((cle_ & 0xFF00) | v); return;
    case kFlagBase + 0xF: ever_ = v & 0x07; return;
    case kIpr: ipr_ = v; return;
    case kIcsr: {
      const bool was_held = isp_reset_held();
      icsr_ = v & 0x3F;
      if (was_held != isp_reset_held() && reset_hook_) reset_hook_(isp_reset_held());
      return;
    }
    case kFedge: fedge_ = v; return;
    case kRedge: redge_ = v; return;
    default: return;  // IOF/EGF read-only
  }
}

}  // namespace h8500
