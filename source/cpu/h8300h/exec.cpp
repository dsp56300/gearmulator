// Instruction handlers ("threaded code") and instruction-cache fill.
//
// Semantics follow the H8/300H programming manual; the condition-code
// details and the timing model are those of the per-instruction core this
// replaces (memory-access based states, one fetch charge per instruction
// word, internal-operation states for the few instructions that have them).

#include "cpu/h8300h/cpu.hpp"

namespace h8300h {

struct ExecImpl {
  // --- registers ----------------------------------------------------------------
  static u8 r8(const Cpu& c, unsigned n) { return c.reg8(n); }
  static u16 r16(const Cpu& c, unsigned n) { return c.reg16(n); }
  static u32 r32(const Cpu& c, unsigned n) { return c.er_[n & 7]; }
  static void w8(Cpu& c, unsigned n, u8 v) { c.set_reg8(n, v); }
  static void w16(Cpu& c, unsigned n, u16 v) { c.set_reg16(n, v); }
  static void w32(Cpu& c, unsigned n, u32 v) { c.er_[n & 7] = v; }
  template <unsigned S> static u32 rreg(const Cpu& c, unsigned n) {
    if constexpr (S == 1) return r8(c, n); else if constexpr (S == 2) return r16(c, n); else return r32(c, n);
  }
  template <unsigned S> static void wreg(Cpu& c, unsigned n, u32 v) {
    if constexpr (S == 1) w8(c, n, u8(v)); else if constexpr (S == 2) w16(c, n, u16(v)); else w32(c, n, v);
  }
  template <unsigned S> static u32 rmem(Cpu& c, u32 a) {
    if constexpr (S == 1) return c.rd8(a); else if constexpr (S == 2) return c.rd16(a); else return c.rd32(a);
  }
  template <unsigned S> static void wmem(Cpu& c, u32 a, u32 v) {
    if constexpr (S == 1) c.wr8(a, u8(v)); else if constexpr (S == 2) c.wr16(a, u16(v)); else c.wr32(a, v);
  }
  template <unsigned S> static s32 sext(u32 v) {
    if constexpr (S == 1) return s32(s8(v)); else if constexpr (S == 2) return s32(s16(v)); else return s32(v);
  }
  template <unsigned S> static constexpr u32 msb() { return S == 1 ? 0x80u : S == 2 ? 0x8000u : 0x80000000u; }
  template <unsigned S> static constexpr u32 mask() { return S == 1 ? 0xFFu : S == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
  template <unsigned S> static constexpr u32 hbit() { return S == 1 ? 0x08u : S == 2 ? 0x800u : 0x08000000u; }

  // --- flags -------------------------------------------------------------------------
  static void set(Cpu& c, u8 bit, bool on) { c.ccr_ = u8(on ? (c.ccr_ | bit) : (c.ccr_ & ~bit)); }
  static bool cf(const Cpu& c) { return (c.ccr_ & kCcrC) != 0; }
  // N / Z from a value, V cleared.
  template <unsigned S> static void flags_val(Cpu& c, u32 v) {
    const s32 sv = sext<S>(v);
    set(c, kCcrV, false);
    set(c, kCcrN, sv < 0);
    set(c, kCcrZ, sv == 0);
  }
  template <unsigned S> static u32 add(Cpu& c, u32 v1, u32 v2, u32 carry = 0, bool keep_z = false) {
    if constexpr (S == 4) {
      const u32 res = v1 + v2 + carry;
      set(c, kCcrZ, res == 0);
      set(c, kCcrN, (res & 0x80000000u) != 0);
      set(c, kCcrC, (((v1 & v2) | (v1 & ~res) | (v2 & ~res)) & 0x80000000u) != 0);
      set(c, kCcrH, (((v1 & v2) | (v1 & ~res) | (v2 & ~res)) & 0x08000000u) != 0);
      set(c, kCcrV, (((v1 & v2 & ~res) | (~v1 & ~v2 & res)) & 0x80000000u) != 0);
      return res;
    } else {
      const s32 a = sext<S>(v1), b = sext<S>(v2);
      const s32 res = a + b + s32(carry & 1);
      if (res & mask<S>()) set(c, kCcrZ, false);
      else if (!keep_z) set(c, kCcrZ, true);
      set(c, kCcrN, (res & msb<S>()) != 0);
      set(c, kCcrC, (((a & b) | (a & ~res) | (b & ~res)) & msb<S>()) != 0);
      set(c, kCcrH, (((a & b) | (a & ~res) | (b & ~res)) & hbit<S>()) != 0);
      set(c, kCcrV, (((a & b & ~res) | (~a & ~b & res)) & msb<S>()) != 0);
      return u32(res) & mask<S>();
    }
  }
  template <unsigned S> static u32 sub(Cpu& c, u32 v1, u32 v2, u32 borrow = 0, bool keep_z = false) {
    if constexpr (S == 4) {
      const u32 res = v1 - v2 - borrow;
      set(c, kCcrZ, res == 0);
      set(c, kCcrN, (res & 0x80000000u) != 0);
      set(c, kCcrC, (((~v1 & v2) | (~v1 & res) | (v2 & res)) & 0x80000000u) != 0);
      set(c, kCcrH, (((~v1 & v2) | (~v1 & res) | (v2 & res)) & 0x08000000u) != 0);
      set(c, kCcrV, (((v1 & ~v2 & ~res) | (~v1 & v2 & res)) & 0x80000000u) != 0);
      return res;
    } else {
      const s32 a = sext<S>(v1), b = sext<S>(v2);
      const s32 res = a - b - s32(borrow & 1);
      if (res & mask<S>()) set(c, kCcrZ, false);
      else if (!keep_z) set(c, kCcrZ, true);
      set(c, kCcrN, (res & msb<S>()) != 0);
      set(c, kCcrC, (((~a & b) | (~a & res) | (b & res)) & msb<S>()) != 0);
      set(c, kCcrH, (((~a & b) | (~a & res) | (b & res)) & hbit<S>()) != 0);
      set(c, kCcrV, (((a & ~b & ~res) | (~a & b & res)) & msb<S>()) != 0);
      return u32(res) & mask<S>();
    }
  }

  // --- prologue / epilogue --------------------------------------------------------------
#define BEGIN            \
  c.cur_ip_ = ip;        \
  c.acc_ = 0
// The next cell is computed first: the handler's data accesses charge acc_.
#define END(next)                              \
  do {                                         \
    const Cell* nx__ = (next);                 \
    EMU_END(c, nx__, ip->cyc + c.acc_);        \
  } while (0)
#define NEXT END(ip + (ip->len >> 1))
#define H(name) static const Cell* name(Cpu& c, const Cell* ip)

  static u32 pc_next(const Cpu& c, const Cell* ip) { return (c.pc_of(ip) + ip->len) & kAddrMask; }
  // Branch to `target`: the fetch of a word the core just fetched is not
  // charged again (a single-word instruction branching to itself).
  static const Cell* branch(Cpu& c, const Cell* ip, u32 target) {
    target &= kAddrMask;
    if (target == ((c.pc_of(ip) + ip->len - 2) & kAddrMask)) c.acc_ -= s32(c.access_states(target, 2));
    return c.cell_at(target);
  }
  static const Cell* invalid(Cpu& c, const Cell* ip) {
    if (!c.faulted_) {
      c.faulted_ = true;
      c.fault_addr_ = c.pc_of(ip);
      c.raise(Cpu::kPendFault);
    }
    return ip;
  }
  H(h_invalid) { BEGIN; END(invalid(c, ip)); }
  H(h_nop) { BEGIN; NEXT; }
  H(h_sleep) {
    BEGIN;
    const bool standby = c.intc_ && (c.intc_->syscr() & 0x80);
    c.standby_ = standby;
    c.sleeping_ = !standby;
    if (standby && c.intc_) c.intc_->enter_software_standby();
    c.raise(Cpu::kPendSleep);
    NEXT;
  }

  // --- moves ---------------------------------------------------------------------------
  template <unsigned S> H(h_mov_rr) {
    BEGIN;
    const u32 v = rreg<S>(c, ip->r >> 4);
    flags_val<S>(c, v);
    wreg<S>(c, ip->r & 15, v);
    NEXT;
  }
  template <unsigned S> H(h_mov_imm) { BEGIN; wreg<S>(c, ip->r & 15, ip->imm); flags_val<S>(c, ip->imm); NEXT; }
  // Memory forms: the address comes from the mode, the direction from Store.
  template <unsigned S, bool Store> static const Cell* mov_mem(Cpu& c, const Cell* ip, u32 a) {
    const unsigned rd = ip->r & 15;
    if constexpr (Store) {
      const u32 v = rreg<S>(c, rd);
      flags_val<S>(c, v);
      wmem<S>(c, a, v);
    } else {
      const u32 v = rmem<S>(c, a);
      wreg<S>(c, rd, v);
      flags_val<S>(c, v);
    }
    return ip + (ip->len >> 1);
  }
  template <unsigned S, bool Store> H(h_mov_abs) { BEGIN; END((mov_mem<S, Store>(c, ip, ip->imm))); }
  template <unsigned S, bool Store> H(h_mov_ind) { BEGIN; END((mov_mem<S, Store>(c, ip, r32(c, ip->r >> 4)))); }
  template <unsigned S, bool Store> H(h_mov_disp) { BEGIN; END((mov_mem<S, Store>(c, ip, r32(c, ip->r >> 4) + ip->imm))); }
  // @ERs+,Rd / Rs,@-ERd: the pointer register is rs (as encoded, bits 7-4).
  template <unsigned S, bool Store> H(h_mov_post) {
    BEGIN;
    const unsigned er = ip->r >> 4, rd = ip->r & 15;
    if constexpr (Store) {
      w32(c, er, r32(c, er) - S);
      const u32 v = rreg<S>(c, rd);
      wmem<S>(c, r32(c, er), v);
      flags_val<S>(c, v);
    } else {
      const u32 v = rmem<S>(c, r32(c, er));
      wreg<S>(c, rd, v);
      w32(c, er, r32(c, er) + S);
      flags_val<S>(c, v);
    }
    NEXT;
  }
  // Long form: bits 6-4 of the fourth byte hold the pointer register in both
  // directions (@ERs+,ERd and ERs,@-ERd), the low bits the data register.
  template <bool Store> H(h_movl_post) {
    BEGIN;
    const unsigned er = ip->r >> 4, rd = ip->r & 15;
    if constexpr (Store) {
      w32(c, er, r32(c, er) - 4);
      const u32 v = r32(c, rd);
      c.wr32(r32(c, er), v);
      flags_val<4>(c, v);
    } else {
      const u32 v = c.rd32(r32(c, er));
      w32(c, rd, v);
      w32(c, er, r32(c, er) + 4);
      flags_val<4>(c, v);
    }
    NEXT;
  }
  // As for every memory form: bits 6-4 of the register byte are the pointer
  // (ERs or ERd), the low bits the data register, in both directions.
  template <bool Store> H(h_movl_ind) {
    BEGIN;
    const unsigned er = ip->r >> 4, rd = ip->r & 15;
    if constexpr (Store) { const u32 v = r32(c, rd); c.wr32(r32(c, er), v); flags_val<4>(c, v); }
    else { const u32 v = c.rd32(r32(c, er)); w32(c, rd, v); flags_val<4>(c, v); }
    NEXT;
  }
  template <bool Store> H(h_movl_disp) {
    BEGIN;
    const unsigned er = ip->r >> 4, rd = ip->r & 15;
    if constexpr (Store) { const u32 v = r32(c, rd); c.wr32(r32(c, er) + ip->imm, v); flags_val<4>(c, v); }
    else { const u32 v = c.rd32(r32(c, er) + ip->imm); w32(c, rd, v); flags_val<4>(c, v); }
    NEXT;
  }
  H(h_movfpe) { BEGIN; const u8 v = c.rd8(ip->imm); w8(c, ip->r & 15, v); flags_val<1>(c, v); NEXT; }
  H(h_movtpe) { BEGIN; const u8 v = r8(c, ip->r & 15); c.wr8(ip->imm, v); flags_val<1>(c, v); NEXT; }

  // --- arithmetic ------------------------------------------------------------------------
  enum class Alu : u8 { Add, Sub, Cmp, And, Or, Xor };
  template <Alu K, unsigned S> static void alu(Cpu& c, unsigned rd, u32 src) {
    const u32 dst = rreg<S>(c, rd);
    if constexpr (K == Alu::Add) wreg<S>(c, rd, add<S>(c, dst, src));
    else if constexpr (K == Alu::Sub) wreg<S>(c, rd, sub<S>(c, dst, src));
    else if constexpr (K == Alu::Cmp) sub<S>(c, dst, src);
    else {
      u32 r;
      if constexpr (K == Alu::And) r = dst & src; else if constexpr (K == Alu::Or) r = dst | src; else r = dst ^ src;
      wreg<S>(c, rd, r);
      flags_val<S>(c, r);
    }
  }
  template <Alu K, unsigned S> H(h_alu_rr) { BEGIN; alu<K, S>(c, ip->r & 15, rreg<S>(c, ip->r >> 4)); NEXT; }
  template <Alu K, unsigned S> H(h_alu_imm) { BEGIN; alu<K, S>(c, ip->r & 15, ip->imm); NEXT; }
  H(h_addx_rr) { BEGIN; w8(c, ip->r & 15, u8(add<1>(c, r8(c, ip->r & 15), r8(c, ip->r >> 4), c.ccr_ & 1, true))); NEXT; }
  H(h_addx_imm) { BEGIN; w8(c, ip->r & 15, u8(add<1>(c, r8(c, ip->r & 15), ip->imm, c.ccr_ & 1, true))); NEXT; }
  H(h_subx_rr) { BEGIN; w8(c, ip->r & 15, u8(sub<1>(c, r8(c, ip->r & 15), r8(c, ip->r >> 4), c.ccr_ & 1, true))); NEXT; }
  H(h_subx_imm) { BEGIN; w8(c, ip->r & 15, u8(sub<1>(c, r8(c, ip->r & 15), ip->imm, c.ccr_ & 1, true))); NEXT; }
  template <unsigned S, bool Dec> H(h_incdec) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    const u32 old = rreg<S>(c, rd);
    const u32 v = (Dec ? old - ip->imm : old + ip->imm) & mask<S>();
    set(c, kCcrN, (v & msb<S>()) != 0);
    set(c, kCcrZ, v == 0);
    set(c, kCcrV, ((Dec ? (old & ~v) : (~old & v)) & msb<S>()) != 0);
    wreg<S>(c, rd, v);
    NEXT;
  }
  H(h_adds) { BEGIN; w32(c, ip->r & 7, r32(c, ip->r & 7) + ip->imm); NEXT; }
  H(h_subs) { BEGIN; w32(c, ip->r & 7, r32(c, ip->r & 7) - ip->imm); NEXT; }
  template <unsigned S> H(h_mulxu) {
    BEGIN;
    const unsigned rs = ip->r >> 4, rd = ip->r & 15;
    if constexpr (S == 2) w16(c, rd, u16(u32(r8(c, rs)) * (r16(c, rd) & 0xFF)));
    else w32(c, rd, u32(r16(c, rs)) * (r32(c, rd) & 0xFFFF));
    NEXT;
  }
  template <unsigned S> H(h_mulxs) {
    BEGIN;
    const unsigned rs = ip->r >> 4, rd = ip->r & 15;
    if constexpr (S == 2) {
      const s32 d = s32(s8(r8(c, rs))) * s32(s8(u8(r16(c, rd))));
      w16(c, rd, u16(d));
      set(c, kCcrN, s16(d) < 0);
      set(c, kCcrZ, s16(d) == 0);
    } else {
      const s32 d = s32(s16(r16(c, rs))) * s32(s16(u16(r32(c, rd))));
      w32(c, rd, u32(d));
      set(c, kCcrN, d < 0);
      set(c, kCcrZ, d == 0);
    }
    NEXT;
  }
  template <unsigned S> H(h_divxu) {
    BEGIN;
    const unsigned rs = ip->r >> 4, rd = ip->r & 15;
    if constexpr (S == 2) {
      const unsigned a = r8(c, rs), b = r16(c, rd);
      unsigned q = 0;
      set(c, kCcrN, (a & 0x80) != 0);
      set(c, kCcrZ, a == 0);
      if (a != 0) q = b / a;
      w8(c, (rd & 7) + 8, u8(q));      // RdL = quotient
      w8(c, rd & 7, u8(b - q * a));    // RdH = remainder
    } else {
      const u32 a = r16(c, rs), b = r32(c, rd);
      u32 q = 0;
      set(c, kCcrN, (a & 0x8000) != 0);
      set(c, kCcrZ, a == 0);
      if (a != 0) q = b / a;
      w16(c, rd, u16(q));
      w16(c, rd + 8, u16(b - q * a));  // Ed = remainder
    }
    NEXT;
  }
  template <unsigned S> H(h_divxs) {
    BEGIN;
    const unsigned rs = ip->r >> 4, rd = ip->r & 15;
    if constexpr (S == 2) {
      const s32 a = s8(r8(c, rs)), b = s16(r16(c, rd));
      s32 q = 0;
      set(c, kCcrZ, a == 0);
      if (a != 0) q = b / a;
      w8(c, (rd & 7) + 8, u8(q));
      w8(c, rd & 7, u8(b - q * a));
      set(c, kCcrN, (((a & ~b) | (~a & b)) & 0x80) != 0);
    } else {
      const s32 a = s16(r16(c, rs)), b = s32(r32(c, rd));
      s32 q = 1;
      set(c, kCcrZ, a == 0);
      if (a != 0) q = b / a;
      else if (b) NEXT;  // divide by zero: no result written
      w16(c, rd, u16(q));
      w16(c, rd + 8, u16(b - q * a));
      set(c, kCcrN, (((a & ~b) | (~a & b)) & 0x8000) != 0);
    }
    NEXT;
  }
  template <unsigned S> H(h_neg) { BEGIN; wreg<S>(c, ip->r & 15, sub<S>(c, 0, rreg<S>(c, ip->r & 15))); NEXT; }
  template <unsigned S> H(h_not) { BEGIN; const u32 v = ~rreg<S>(c, ip->r & 15) & mask<S>(); wreg<S>(c, ip->r & 15, v); flags_val<S>(c, v); NEXT; }
  template <unsigned S> H(h_extu) {
    BEGIN;
    const u32 v = rreg<S>(c, ip->r & 15) & (S == 2 ? 0xFFu : 0xFFFFu);
    wreg<S>(c, ip->r & 15, v);
    set(c, kCcrN, false);
    set(c, kCcrV, false);
    set(c, kCcrZ, v == 0);
    NEXT;
  }
  template <unsigned S> H(h_exts) {
    BEGIN;
    u32 v = rreg<S>(c, ip->r & 15) & (S == 2 ? 0xFFu : 0xFFFFu);
    if (v & (S == 2 ? 0x80u : 0x8000u)) v |= (S == 2 ? 0xFF00u : 0xFFFF0000u);
    wreg<S>(c, ip->r & 15, v);
    flags_val<S>(c, v);
    NEXT;
  }

  // --- shifts and rotates ------------------------------------------------------------------
  template <unsigned S> static void flags_masked(Cpu& c, u32 v) {
    v &= mask<S>();
    set(c, kCcrV, false);
    set(c, kCcrN, (v & msb<S>()) != 0);
    set(c, kCcrZ, v == 0);
  }
  template <unsigned S> H(h_shal) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    constexpr unsigned bits = S * 8;
    const bool old_sign = (v & msb<S>()) != 0;
    set(c, kCcrC, (v & (1u << (bits - 1))) != 0);
    v = (v << 1) & mask<S>();
    const bool new_sign = (v & msb<S>()) != 0;
    set(c, kCcrV, old_sign != new_sign);
    set(c, kCcrZ, v == 0);
    set(c, kCcrN, new_sign);
    wreg<S>(c, rd, v);
    NEXT;
  }
  template <unsigned S> H(h_shll) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    set(c, kCcrC, (v & (1u << (S * 8 - 1))) != 0);
    v <<= 1;
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_shar) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    s32 v = sext<S>(rreg<S>(c, rd));
    set(c, kCcrC, (v & 1) != 0);
    v >>= 1;
    set(c, kCcrV, false);
    set(c, kCcrN, v < 0);
    set(c, kCcrZ, v == 0);
    wreg<S>(c, rd, u32(v) & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_shlr) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    set(c, kCcrC, (v & 1) != 0);
    v >>= 1;
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_rotl) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    const u32 t = v >> (S * 8 - 1);
    v = (v << 1) | t;
    set(c, kCcrC, (t & 1) != 0);
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_rotxl) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    const u32 nc = (v >> (S * 8 - 1)) & 1;
    v = (v << 1) | (cf(c) ? 1u : 0u);
    set(c, kCcrC, nc != 0);
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_rotr) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    const u32 t = v & 1;
    v = (v >> 1) | (t << (S * 8 - 1));
    set(c, kCcrC, t != 0);
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }
  template <unsigned S> H(h_rotxr) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    u32 v = rreg<S>(c, rd);
    const u32 r = v & 1;
    v >>= 1;
    if (cf(c)) v |= 1u << (S * 8 - 1);
    set(c, kCcrC, r != 0);
    flags_masked<S>(c, v);
    wreg<S>(c, rd, v & mask<S>());
    NEXT;
  }

  // --- bit manipulation ----------------------------------------------------------------------
  // Applies the operation encoded in x to `byte`; returns whether it changed.
  static bool bit_apply(Cpu& c, const Cell* ip, u8& byte) {
    const BitKind k = BitKind(ip->x & 15);
    const unsigned n = (ip->x & 0x80) ? (r8(c, ip->r >> 4) & 7) : ((ip->x >> 4) & 7);
    const u8 bit = u8(1u << n);
    const unsigned v = (byte >> n) & 1;
    switch (k) {
      case BitKind::Bset: byte |= bit; return true;
      case BitKind::Bnot: byte ^= bit; return true;
      case BitKind::Bclr: byte &= u8(~bit); return true;
      case BitKind::Btst: set(c, kCcrZ, (byte & bit) == 0); return false;
      case BitKind::Bst: byte = u8((byte & ~bit) | ((c.ccr_ & 1) ? bit : 0)); return true;
      case BitKind::Bist: byte = u8((byte & ~bit) | ((c.ccr_ & 1) ? 0 : bit)); return true;
      case BitKind::Bor: set(c, kCcrC, ((c.ccr_ & 1) | v) != 0); return false;
      case BitKind::Bior: set(c, kCcrC, ((c.ccr_ & 1) | (v ^ 1)) != 0); return false;
      case BitKind::Bxor: set(c, kCcrC, ((c.ccr_ & 1) ^ v) != 0); return false;
      case BitKind::Bixor: set(c, kCcrC, ((c.ccr_ & 1) ^ (v ^ 1)) != 0); return false;
      case BitKind::Band: set(c, kCcrC, ((c.ccr_ & 1) & v) != 0); return false;
      case BitKind::Biand: set(c, kCcrC, ((c.ccr_ & 1) & (v ^ 1)) != 0); return false;
      case BitKind::Bld: set(c, kCcrC, v != 0); return false;
      default: set(c, kCcrC, (v ^ 1) != 0); return false;  // Bild
    }
  }
  H(h_bit_reg) {
    BEGIN;
    u8 b = r8(c, ip->r & 15);
    if (bit_apply(c, ip, b)) w8(c, ip->r & 15, b);
    NEXT;
  }
  static const Cell* bit_mem(Cpu& c, const Cell* ip, u32 a) {
    u8 b = c.rd8(a);
    if (bit_apply(c, ip, b)) c.wr8(a, b);
    return ip + (ip->len >> 1);
  }
  H(h_bit_ind) { BEGIN; END(bit_mem(c, ip, r32(c, ip->r & 7))); }
  H(h_bit_abs) { BEGIN; END(bit_mem(c, ip, ip->imm)); }

  // --- branches -------------------------------------------------------------------------------
  static bool condition(const Cpu& c, unsigned cc) {
    const u8 ccr = c.ccr_;
    unsigned test;
    switch ((cc >> 1) & 7) {
      case 0: test = 0; break;
      case 1: test = ccr & (kCcrC | kCcrZ); break;
      case 2: test = ccr & kCcrC; break;
      case 3: test = ccr & kCcrZ; break;
      case 4: test = ccr & kCcrV; break;
      case 5: test = ccr & kCcrN; break;
      case 6: test = ((ccr >> 2) ^ ccr) & kCcrV; break;
      default: test = (((ccr >> 2) ^ ccr) & kCcrV) | (ccr & kCcrZ); break;
    }
    return (test != 0) == ((cc & 1) != 0);
  }
  H(h_bcc) {
    BEGIN;
    u32 pc = pc_next(c, ip);
    if (condition(c, ip->x)) pc = (pc + ip->imm) & kAddrMask;
    c.acc_ += s32(c.access_states(pc, 2));  // Bcc performs a second instruction-fetch cycle
    END(branch(c, ip, pc));
  }
  H(h_jmp_er) { BEGIN; END(branch(c, ip, r32(c, ip->r & 7))); }
  H(h_jmp_abs) { BEGIN; END(branch(c, ip, ip->imm)); }
  H(h_jmp_ind8) { BEGIN; END(branch(c, ip, c.rd32(ip->imm & ~1u))); }
  H(h_jsr_er) { BEGIN; c.push32(pc_next(c, ip)); END(branch(c, ip, r32(c, ip->r & 7))); }
  H(h_jsr_abs) { BEGIN; c.push32(pc_next(c, ip)); END(branch(c, ip, ip->imm)); }
  H(h_jsr_ind8) { BEGIN; c.push32(pc_next(c, ip)); END(branch(c, ip, c.rd32(ip->imm & ~1u))); }
  H(h_bsr8) {
    BEGIN;
    const u32 next = pc_next(c, ip), target = (next + ip->imm) & kAddrMask;
    c.push32(next);
    c.acc_ += s32(c.access_states(target, 2));
    END(branch(c, ip, target));
  }
  H(h_bsr16) {
    BEGIN;
    const u32 next = pc_next(c, ip);
    c.push32(next);
    END(branch(c, ip, next + ip->imm));
  }
  H(h_rts) { BEGIN; END(branch(c, ip, c.pop32())); }
  H(h_rte) {
    BEGIN;
    const u32 p = c.pop32();
    c.ccr_ = u8(p >> 24);
    c.hold_off();
    c.reeval_irq();
    END(branch(c, ip, p));
  }
  H(h_trapa) {
    BEGIN;
    c.pc_ = pc_next(c, ip);
    c.acc_ += s32(c.enter_exception(8 + (ip->imm & 3)));
    c.reeval_irq();
    END(c.cell_at(c.pc_));
  }

  // --- CCR -----------------------------------------------------------------------------------
  static void load_ccr(Cpu& c, u8 v) { c.ccr_ = v; c.hold_off(); c.reeval_irq(); }
  H(h_ldc_imm) { BEGIN; load_ccr(c, u8(ip->imm)); NEXT; }
  H(h_ldc_reg) { BEGIN; load_ccr(c, r8(c, ip->r & 15)); NEXT; }
  H(h_stc_reg) { BEGIN; w8(c, ip->r & 15, c.ccr_); NEXT; }
  H(h_andc) { BEGIN; load_ccr(c, u8(c.ccr_ & ip->imm)); NEXT; }
  H(h_orc) { BEGIN; load_ccr(c, u8(c.ccr_ | ip->imm)); NEXT; }
  H(h_xorc) { BEGIN; load_ccr(c, u8(c.ccr_ ^ ip->imm)); NEXT; }
  // LDC/STC memory forms read / write a word; the CCR is its low byte.
  H(h_ldc_abs) { BEGIN; load_ccr(c, u8(c.rd16(ip->imm))); NEXT; }
  H(h_stc_abs) { BEGIN; c.wr16(ip->imm, c.ccr_); NEXT; }
  H(h_ldc_ind) { BEGIN; load_ccr(c, u8(c.rd16(r32(c, ip->r >> 4)))); NEXT; }
  H(h_stc_ind) { BEGIN; c.wr16(r32(c, ip->r >> 4), c.ccr_); NEXT; }
  H(h_ldc_disp) { BEGIN; load_ccr(c, u8(c.rd16(r32(c, ip->r >> 4) + ip->imm))); NEXT; }
  H(h_stc_disp) { BEGIN; c.wr16(r32(c, ip->r >> 4) + ip->imm, c.ccr_); NEXT; }
  H(h_ldc_post) {
    BEGIN;
    const unsigned r = ip->r >> 4;
    const u8 v = u8(c.rd16(r32(c, r)));
    w32(c, r, r32(c, r) + 2);
    load_ccr(c, v);
    NEXT;
  }
  H(h_stc_pre) {
    BEGIN;
    const unsigned r = ip->r >> 4;
    w32(c, r, r32(c, r) - 2);
    c.wr16(r32(c, r), c.ccr_);
    NEXT;
  }

  // --- decimal, block move -----------------------------------------------------------------
  H(h_daa) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    const u8 v = r8(c, rd);
    const int l = v & 0xF, u = (v >> 4) & 0xF;
    const bool cy = (c.ccr_ & kCcrC) != 0, h = (c.ccr_ & kCcrH) != 0;
    int toadd = 0;
    if (cy) { if (h) { if (u <= 3) toadd = (l <= 3) ? 0x66 : 0x00; } else { if (u <= 2) toadd = (l <= 9) ? 0x60 : 0x66; } }
    else { if (h) { if (l <= 3) toadd = (u <= 9) ? 0x06 : 0x66; } else { if (l <= 9) toadd = (u <= 9) ? 0x00 : 0x60; else toadd = (u <= 8) ? 0x06 : 0x66; } }
    const u8 r = u8(v + toadd);
    w8(c, rd, r);
    flags_val<1>(c, r);
    if (toadd > 6) set(c, kCcrC, true);
    NEXT;
  }
  H(h_das) {
    BEGIN;
    const unsigned rd = ip->r & 15;
    const u8 v = r8(c, rd);
    const int l = v & 0xF, u = (v >> 4) & 0xF;
    const bool cy = (c.ccr_ & kCcrC) != 0, h = (c.ccr_ & kCcrH) != 0;
    int toadd = 0;
    if (cy) { if (h) { if (u >= 6 && l >= 6) toadd = 0x9A; } else { if (u >= 7 && l <= 9) toadd = 0xA0; } }
    else { if (h) { if (u <= 8 && l >= 6) toadd = 0xFA; } }
    w8(c, rd, u8(add<1>(c, v, u32(toadd))));
    set(c, kCcrC, cy);
    NEXT;
  }
  template <unsigned S> H(h_eepmov) {
    BEGIN;
    if constexpr (S == 2) {
      while (r16(c, 4) != 0) {
        c.wr8(r32(c, 6), c.rd8(r32(c, 5)));
        w32(c, 5, r32(c, 5) + 1);
        w32(c, 6, r32(c, 6) + 1);
        w16(c, 4, u16(r16(c, 4) - 1));
      }
    } else {
      while (r8(c, 12) != 0) {  // R4L
        c.wr8(r32(c, 6), c.rd8(r32(c, 5)));
        w32(c, 5, r32(c, 5) + 1);
        w32(c, 6, r32(c, 6) + 1);
        w8(c, 12, u8(r8(c, 12) - 1));
      }
    }
    NEXT;
  }

  // ---------------------------------------------------------------------------
  // Handler selection

  template <template <unsigned> class T> static Handler by_size(u8 s) {
    // (helper for the three-size families; specialised below through lambdas)
    return s == 1 ? &T<1>::f : s == 2 ? &T<2>::f : &T<4>::f;
  }
#define SIZED(fn, s) ((s) == 1 ? &fn<1> : (s) == 2 ? &fn<2> : &fn<4>)
#define SIZED_STORE(fn, s, st) ((st) ? SIZED_S(fn, s, true) : SIZED_S(fn, s, false))
#define SIZED_S(fn, s, st) ((s) == 1 ? &fn<1, st> : (s) == 2 ? &fn<2, st> : &fn<4, st>)
  template <Alu K> static Handler alu_rr(u8 s) { return s == 1 ? &h_alu_rr<K, 1> : s == 2 ? &h_alu_rr<K, 2> : &h_alu_rr<K, 4>; }
  template <Alu K> static Handler alu_imm(u8 s) { return s == 1 ? &h_alu_imm<K, 1> : s == 2 ? &h_alu_imm<K, 2> : &h_alu_imm<K, 4>; }

  static Handler select(const DecodedInsn& d) {
    const u8 s = d.x;
    switch (d.op) {
      case Op::Invalid: return &h_invalid;
      case Op::Nop: return &h_nop;
      case Op::Sleep: return &h_sleep;
      case Op::MovRR: return SIZED(h_mov_rr, s);
      case Op::MovImm: return SIZED(h_mov_imm, s);
      case Op::MovAbs8: case Op::MovAbs16: case Op::MovAbs24:
        return d.store ? SIZED_S(h_mov_abs, s, true) : SIZED_S(h_mov_abs, s, false);
      case Op::MovInd:
        if (s == 4) return d.store ? &h_movl_ind<true> : &h_movl_ind<false>;
        return d.store ? SIZED_S(h_mov_ind, s, true) : SIZED_S(h_mov_ind, s, false);
      case Op::MovDisp16: case Op::MovDisp24:
        if (s == 4) return d.store ? &h_movl_disp<true> : &h_movl_disp<false>;
        return d.store ? SIZED_S(h_mov_disp, s, true) : SIZED_S(h_mov_disp, s, false);
      case Op::MovPost:
        if (s == 4) return d.store ? &h_movl_post<true> : &h_movl_post<false>;
        return d.store ? SIZED_S(h_mov_post, s, true) : SIZED_S(h_mov_post, s, false);
      case Op::Movfpe: return &h_movfpe;
      case Op::Movtpe: return &h_movtpe;
      case Op::Add: return alu_rr<Alu::Add>(s);
      case Op::Sub: return alu_rr<Alu::Sub>(s);
      case Op::Cmp: return alu_rr<Alu::Cmp>(s);
      case Op::And: return alu_rr<Alu::And>(s);
      case Op::Or: return alu_rr<Alu::Or>(s);
      case Op::Xor: return alu_rr<Alu::Xor>(s);
      case Op::AddImm: return alu_imm<Alu::Add>(s);
      case Op::SubImm: return alu_imm<Alu::Sub>(s);
      case Op::CmpImm: return alu_imm<Alu::Cmp>(s);
      case Op::AndImm: return alu_imm<Alu::And>(s);
      case Op::OrImm: return alu_imm<Alu::Or>(s);
      case Op::XorImm: return alu_imm<Alu::Xor>(s);
      case Op::Addx: return &h_addx_rr;
      case Op::AddxImm: return &h_addx_imm;
      case Op::Subx: return &h_subx_rr;
      case Op::SubxImm: return &h_subx_imm;
      case Op::Inc: return SIZED_S(h_incdec, s, false);
      case Op::Dec: return SIZED_S(h_incdec, s, true);
      case Op::Adds: return &h_adds;
      case Op::Subs: return &h_subs;
      case Op::Mulxu: return s == 2 ? &h_mulxu<2> : &h_mulxu<4>;
      case Op::Mulxs: return s == 2 ? &h_mulxs<2> : &h_mulxs<4>;
      case Op::Divxu: return s == 2 ? &h_divxu<2> : &h_divxu<4>;
      case Op::Divxs: return s == 2 ? &h_divxs<2> : &h_divxs<4>;
      case Op::Neg: return SIZED(h_neg, s);
      case Op::Not: return SIZED(h_not, s);
      case Op::Extu: return s == 2 ? &h_extu<2> : &h_extu<4>;
      case Op::Exts: return s == 2 ? &h_exts<2> : &h_exts<4>;
      case Op::Shal: return SIZED(h_shal, s);
      case Op::Shar: return SIZED(h_shar, s);
      case Op::Shll: return SIZED(h_shll, s);
      case Op::Shlr: return SIZED(h_shlr, s);
      case Op::Rotl: return SIZED(h_rotl, s);
      case Op::Rotr: return SIZED(h_rotr, s);
      case Op::Rotxl: return SIZED(h_rotxl, s);
      case Op::Rotxr: return SIZED(h_rotxr, s);
      case Op::BitReg: return &h_bit_reg;
      case Op::BitInd: return &h_bit_ind;
      case Op::BitAbs: return &h_bit_abs;
      case Op::Bcc8: case Op::Bcc16: return &h_bcc;
      case Op::JmpEr: return &h_jmp_er;
      case Op::JmpAbs: return &h_jmp_abs;
      case Op::JmpInd8: return &h_jmp_ind8;
      case Op::JsrEr: return &h_jsr_er;
      case Op::JsrAbs: return &h_jsr_abs;
      case Op::JsrInd8: return &h_jsr_ind8;
      case Op::Bsr8: return &h_bsr8;
      case Op::Bsr16: return &h_bsr16;
      case Op::Rts: return &h_rts;
      case Op::Rte: return &h_rte;
      case Op::Trapa: return &h_trapa;
      case Op::LdcImm: return &h_ldc_imm;
      case Op::LdcReg: return &h_ldc_reg;
      case Op::StcReg: return &h_stc_reg;
      case Op::LdcAbs16: case Op::LdcAbs24: return &h_ldc_abs;
      case Op::StcAbs16: case Op::StcAbs24: return &h_stc_abs;
      case Op::LdcInd: return &h_ldc_ind;
      case Op::StcInd: return &h_stc_ind;
      case Op::LdcDisp16: case Op::LdcDisp24: return &h_ldc_disp;
      case Op::StcDisp16: case Op::StcDisp24: return &h_stc_disp;
      case Op::LdcPost: return &h_ldc_post;
      case Op::StcPre: return &h_stc_pre;
      case Op::Andc: return &h_andc;
      case Op::Orc: return &h_orc;
      case Op::Xorc: return &h_xorc;
      case Op::Daa: return &h_daa;
      case Op::Das: return &h_das;
      case Op::Eepmov: return s == 2 ? &h_eepmov<2> : &h_eepmov<1>;
    }
    return &h_invalid;
  }
#undef SIZED
#undef SIZED_S
#undef SIZED_STORE

  // Internal-operation states (manual 7.3, the "I" column) beyond fetches and
  // data accesses.
  static u32 internal_states(const DecodedInsn& d) {
    switch (d.op) {
      case Op::MovPost: case Op::LdcPost: case Op::StcPre: return 2;
      case Op::Mulxu: case Op::Mulxs: case Op::Divxu: case Op::Divxs: return d.x == 2 ? 12 : 20;
      case Op::Rts: case Op::Rte: case Op::Bsr16: case Op::Bcc16: case Op::JmpAbs: case Op::JmpInd8: case Op::JsrAbs: return 2;
      default: return 0;
    }
  }

  static const Cell* cross(Cpu& c, const Cell* ip) {
    const Cell* next = c.cells_for(c.pc_of(ip));
    EMU_GOTO(c, next);
  }

  static const Cell* fill(Cpu& c, const Cell* ip) {
    const u32 pc = c.pc_of(ip);
    const DecodedInsn d = c.decode_at(pc);
    Cell cell{};
    cell.fn = select(d);
    cell.imm = d.imm;
    cell.r = u8((d.rs << 4) | (d.rd & 15));
    cell.len = d.len;
    cell.x = d.x;
    // The Bcc / branch handlers keep the condition / size where the decoder put it.
    const u32 st = c.fetch_states(pc, d.len >> 1) + internal_states(d);
    cell.cyc = u8(st > 255 ? 255 : st);
    c.bus_.mark_code(pc);
    c.bus_.mark_code((pc + d.len - 1) & kAddrMask);
    *const_cast<Cell*>(ip) = cell;
    EMU_GOTO(c, ip);
  }
#undef BEGIN
#undef END
#undef NEXT
#undef H
};

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::fill(cpu, ip)); }
const Cell* cell_cross(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::cross(cpu, ip)); }
}  // namespace detail

}  // namespace h8300h
