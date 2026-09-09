#include "cpu/h8500/ports.hpp"

#include "common/iomux.hpp"

namespace h8500 {

namespace {
// Register map: port number (1-8) for DDR and DR offsets within the port block;
// 0 = none.  The data registers sit at the same offsets on both chips; the
// direction registers do not, because the H8/532 drives its data bus from port
// 3 (no P3DDR) and has a P7DDR where the H8/510 has P8DDR.
constexpr u8 kDrPort[16] = {0, 0, 1, 2, 0, 0, 3, 4, 0, 0, 5, 6, 0, 0, 7, 8};
constexpr u8 kDdrPort510[16] = {1, 2, 0, 0, 3, 4, 0, 0, 5, 6, 0, 0, 0, 8, 0, 0};
constexpr u8 kDdrPort532[16] = {1, 2, 0, 0, 0, 4, 0, 0, 5, 6, 0, 0, 7, 0, 0, 0};
}  // namespace

Ports::Ports(const ChipConfig& cfg, u32 base) : cfg_(cfg), base_(base) {}

const u8* Ports::ddr_map() const {
  return cfg_.model == ChipModel::H8_532 ? kDdrPort532 : kDdrPort510;
}

// The one port that has no output driver: port 7 on the H8/510, port 8 on the
// H8/532.
unsigned Ports::input_only_port() const { return cfg_.model == ChipModel::H8_532 ? 8 : 7; }

void Ports::map(emu::IoMux& mux) {
  mux.assign(base_, 16, this);
  // The H8/532 keeps port 9 on its own, away from the block (appendix B).
  if (p9_ddr_addr_) {
    mux.assign(p9_ddr_addr_, 1, this);
    mux.assign(u32(p9_ddr_addr_ + 1), 1, this);
  }
}

void Ports::reset() {
  for (P& p : p_) { p.ddr = 0; p.dr = 0; }
}

u8 Ports::read_dr(unsigned port) const {
  const P& p = p_[idx(port)];
  if (cfg_.model == ChipModel::H8_532) {
    // Expanded modes: ports 1 and 2 are the address bus, port 3 the data bus.
    // Port 8 is input only.  (H8/532 hardware manual, section 8.)
    const bool expanded = cfg_.mode != 7;
    switch (port) {
      case 1: case 2: if (expanded) return p.dr; break;
      case 3: if (expanded) return 0xFF; break;
      case 8: return p.pins;
      default: break;
    }
    return u8((p.dr & p.ddr) | (p.pins & ~p.ddr));
  }
  const bool bus16 = cfg_.mode == 2 || cfg_.mode == 4;
  const bool max = cfg_.max_mode();
  switch (port) {
    case 1: if (bus16) return 0xFF; break;          // data bus D7-D0 (table C-1)
    case 2: if (max) return p.dr; break;            // address bus A23-A16 (table C-2)
    case 7: return u8(p.pins & 0x0F);               // input only, 4 pins
    default: break;
  }
  return u8((p.dr & p.ddr) | (p.pins & ~p.ddr));
}

u8 Ports::read8(u32 addr) {
  if (p9_ddr_addr_ && addr == p9_ddr_addr_ + 1) {
    const u8 v = read_dr(9);
    return read_hook_ ? read_hook_(9, v) : v;
  }
  if (p9_ddr_addr_ && addr == p9_ddr_addr_) return 0xFF;
  const u32 off = addr - base_;
  if (off >= 16) return 0xFF;
  if (ddr_map()[off]) return 0xFF;  // DDRs read as all ones
  if (const unsigned port = kDrPort[off]) {
    const u8 v = read_dr(port);
    return read_hook_ ? read_hook_(port, v) : v;
  }
  return 0xFF;
}

void Ports::write8(u32 addr, u8 v) {
  if (p9_ddr_addr_ && (addr == p9_ddr_addr_ || addr == p9_ddr_addr_ + 1)) {
    P& p = p_[idx(9)];
    if (addr == p9_ddr_addr_) p.ddr = v; else p.dr = v;
    if (hook_) hook_(9, p.dr, p.ddr);
    return;
  }
  const u32 off = addr - base_;
  if (off >= 16) return;
  if (const unsigned port = ddr_map()[off]) {
    p_[idx(port)].ddr = v;
    if (hook_) hook_(port, p_[idx(port)].dr, v);
    return;
  }
  if (const unsigned port = kDrPort[off]) {
    if (port == input_only_port()) return;  // no output latch to write
    p_[idx(port)].dr = v;
    if (hook_) hook_(port, v, p_[idx(port)].ddr);
  }
}

// ---------------------------------------------------------------------------

void SysRegs::map(emu::IoMux& mux) {
  mux.assign(0xFED8, 1, this);
  mux.assign(0xFF14, 1, this);
  mux.assign(0xFF16, 2, this);
  mux.assign(0xFF19, 3, this);
}

void SysRegs::reset() {
  rfshcr_ = 0xD8;
  wcr_ = 0xF3;
  arbt_ = 0xFF;
  ar3t_ = 0x00;
  sbycr_ = 0x7F;
  brcr_ = 0xFE;
}

u8 SysRegs::read8(u32 addr) {
  switch (addr) {
    case 0xFED8: return rfshcr_;
    case 0xFF14: return u8(wcr_ | 0xF0);
    case 0xFF16: return arbt_;
    case 0xFF17: return ar3t_;
    case 0xFF19: return u8(0xC0 | (cfg_.mode & 7));  // MDCR: bits 7-6 read 1, MDS2-0 = mode pins
    case 0xFF1A: return u8(sbycr_ | 0x7F);
    case 0xFF1B: return u8(brcr_ | 0xFE);
    default: return 0xFF;
  }
}

void SysRegs::write8(u32 addr, u8 v) {
  switch (addr) {
    case 0xFED8: rfshcr_ = v; break;
    case 0xFF14: wcr_ = u8(v & 0x0F); break;
    case 0xFF16: arbt_ = v; break;
    case 0xFF17: ar3t_ = v; break;
    case 0xFF1A: sbycr_ = u8(v & 0x80); break;  // TODO(standby): software standby mode
    case 0xFF1B: brcr_ = u8(v & 0x01); break;
    default: break;
  }
}

}  // namespace h8500
