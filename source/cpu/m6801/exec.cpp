// Instruction handlers ("threaded code") and instruction-cache fill.
//
// Semantics follow the MC6801 reference manual, the HD6301V1 manual and the
// M68HC11 reference manual; the condition codes of every instruction agree
// with MAME's m6800 and mc68hc11 cores. Each
// handler is a template over its operation, register and addressing mode, so
// no decoding is left at run time: the operand comes from the cell.
#include <array>
#include <utility>

#include "cpu/m6801/cpu.hpp"

namespace m6801 {

struct ExecImpl {
  // --- registers and flags ------------------------------------------------------
  template <unsigned R> static u8& acc(Cpu& c) {
    if constexpr (R == 0) return c.a_;
    else return c.b_;
  }
  // The index register of the X instructions: 0 = X, 1 = Y (HC11).
  template <unsigned IR> static u16& ix(Cpu& c) {
    if constexpr (IR == 0) return c.x_;
    else return c.y_;
  }
  static void set_nz8(Cpu& c, u8 v) {
    c.cc_ = u8((c.cc_ & ~(kN | kZ | kV)) | ((v & 0x80) ? kN : 0) | (v ? 0 : kZ));
  }
  static void set_nz16(Cpu& c, u16 v) {
    c.cc_ = u8((c.cc_ & ~(kN | kZ | kV)) | ((v & 0x8000) ? kN : 0) | (v ? 0 : kZ));
  }
  // `r` carries the carry / borrow out of bit 7 in bit 8; the overflow is
  // the carry into bit 7 against the carry out of it.
  static u8 flags8(Cpu& c, unsigned a, unsigned b, unsigned r, bool half) {
    u8 f = u8(c.cc_ & ~(kN | kZ | kV | kC | (half ? kH : 0)));
    if (half && ((a ^ b ^ r) & 0x10)) f |= kH;
    if (r & 0x80) f |= kN;
    if (!(r & 0xFF)) f |= kZ;
    if ((a ^ b ^ r ^ (r >> 1)) & 0x80) f |= kV;
    if (r & 0x100) f |= kC;
    c.cc_ = f;
    return u8(r);
  }
  static u8 add8(Cpu& c, u8 a, u8 b, unsigned carry) { return flags8(c, a, b, unsigned(a) + b + carry, true); }
  static u8 sub8(Cpu& c, u8 a, u8 b, unsigned borrow) { return flags8(c, a, b, unsigned(a) - b - borrow, false); }
  static u16 flags16(Cpu& c, u32 a, u32 b, u32 r) {
    u8 f = u8(c.cc_ & ~(kN | kZ | kV | kC));
    if (r & 0x8000) f |= kN;
    if (!(r & 0xFFFF)) f |= kZ;
    if ((a ^ b ^ r ^ (r >> 1)) & 0x8000) f |= kV;
    if (r & 0x10000) f |= kC;
    c.cc_ = f;
    return u16(r);
  }
  static u16 add16(Cpu& c, u16 a, u16 b) { return flags16(c, a, b, u32(a) + b); }
  static u16 sub16(Cpu& c, u16 a, u16 b) { return flags16(c, a, b, u32(a) - b); }

  template <unsigned Cc> static bool condition(u8 f) {
    const bool n = f & kN, z = f & kZ, v = f & kV, cy = f & kC;
    if constexpr (Cc == 0x0) return true;           // BRA
    else if constexpr (Cc == 0x1) return false;     // BRN
    else if constexpr (Cc == 0x2) return !(cy || z);  // BHI
    else if constexpr (Cc == 0x3) return cy || z;   // BLS
    else if constexpr (Cc == 0x4) return !cy;       // BCC
    else if constexpr (Cc == 0x5) return cy;        // BCS
    else if constexpr (Cc == 0x6) return !z;        // BNE
    else if constexpr (Cc == 0x7) return z;         // BEQ
    else if constexpr (Cc == 0x8) return !v;        // BVC
    else if constexpr (Cc == 0x9) return v;         // BVS
    else if constexpr (Cc == 0xA) return !n;        // BPL
    else if constexpr (Cc == 0xB) return n;         // BMI
    else if constexpr (Cc == 0xC) return n == v;    // BGE
    else if constexpr (Cc == 0xD) return n != v;    // BLT
    else if constexpr (Cc == 0xE) return !z && n == v;  // BGT
    else return z || n != v;                        // BLE
  }

  // --- operands -------------------------------------------------------------------
  template <Mode M> static u16 ea(const Cpu& c, const Cell* ip) {
    if constexpr (M == Mode::Idx) return u16(c.x_ + ip->imm);
    else if constexpr (M == Mode::Idy) return u16(c.y_ + ip->imm);
    else return ip->imm;
  }
  template <Mode M> static u8 operand8(Cpu& c, const Cell* ip) {
    if constexpr (M == Mode::Imm) return u8(ip->imm);
    else return c.rd8(ea<M>(c, ip), ip->d0);
  }
  template <Mode M> static u16 operand16(Cpu& c, const Cell* ip) {
    if constexpr (M == Mode::Imm) return ip->imm;
    else return c.rd16(ea<M>(c, ip), ip->d0);
  }

  // --- prologue / epilogue -----------------------------------------------------------
  // Length and cycles are read before the handler's stores: a store into the
  // instruction's own bytes blanks its cell.
#define H(name) static const Cell* name(Cpu& c, const Cell* ip)
#define BEGIN                                    \
  [[maybe_unused]] const unsigned len_ = ip->len; \
  const unsigned cyc_ = ip->cyc;                 \
  c.cur_ip_ = ip
#define NEXT EMU_END(c, ip + len_, cyc_)
#define JUMP(target) EMU_END(c, c.cell_at(target), cyc_)

  // --- 8-bit accumulator operations ---------------------------------------------------
  template <Op K> static void alu8(Cpu& c, u8& r, u8 m) {
    if constexpr (K == Op::Sub) r = sub8(c, r, m, 0);
    else if constexpr (K == Op::Cmp) sub8(c, r, m, 0);
    else if constexpr (K == Op::Sbc) r = sub8(c, r, m, c.cc_ & kC);
    else if constexpr (K == Op::And) { r &= m; set_nz8(c, r); }
    else if constexpr (K == Op::Bit) set_nz8(c, u8(r & m));
    else if constexpr (K == Op::Lda) { r = m; set_nz8(c, r); }
    else if constexpr (K == Op::Eor) { r ^= m; set_nz8(c, r); }
    else if constexpr (K == Op::Adc) r = add8(c, r, m, c.cc_ & kC);
    else if constexpr (K == Op::Ora) { r |= m; set_nz8(c, r); }
    else r = add8(c, r, m, 0);  // Add
  }
  template <Op K, unsigned R, Mode M> H(h_alu8) {
    BEGIN;
    alu8<K>(c, acc<R>(c), operand8<M>(c, ip));
    NEXT;
  }
  template <unsigned R, Mode M> H(h_sta) {
    BEGIN;
    const u8 v = acc<R>(c);
    set_nz8(c, v);
    c.wr8(ea<M>(c, ip), v, ip->d0);
    NEXT;
  }

  // --- 16-bit operations ----------------------------------------------------------------
  template <Op K, unsigned IR, Mode M> H(h_alu16) {
    BEGIN;
    const u16 m = operand16<M>(c, ip);
    if constexpr (K == Op::Subd) c.set_d(sub16(c, c.d(), m));
    else if constexpr (K == Op::Addd) c.set_d(add16(c, c.d(), m));
    else if constexpr (K == Op::Cpd) sub16(c, c.d(), m);
    else if constexpr (K == Op::Cpx) sub16(c, ix<IR>(c), m);
    else if constexpr (K == Op::Ldd) { c.set_d(m); set_nz16(c, m); }
    else if constexpr (K == Op::Ldx) { ix<IR>(c) = m; set_nz16(c, m); }
    else { c.s_ = m; set_nz16(c, m); }  // Lds
    NEXT;
  }
  template <Op K, unsigned IR, Mode M> H(h_st16) {
    BEGIN;
    const u16 v = K == Op::Std ? c.d() : K == Op::Stx ? ix<IR>(c) : c.s_;
    set_nz16(c, v);
    c.wr16(ea<M>(c, ip), v, ip->d0);
    NEXT;
  }

  // --- read-modify-write --------------------------------------------------------------------
  template <Op K> static u8 unary(Cpu& c, u8 t) {
    if constexpr (K == Op::Neg) {
      return sub8(c, 0, t, 0);
    } else {
      const unsigned cin = c.cc_ & kC;
      u8 f = u8(c.cc_ & ~(kN | kZ | kV | kC));
      u8 r;
      if constexpr (K == Op::Com) { r = u8(~t); f |= kC; }
      else if constexpr (K == Op::Lsr) { r = u8(t >> 1); if (t & 1) f |= kC | kV; }
      else if constexpr (K == Op::Ror) { r = u8((t >> 1) | (cin << 7)); if (t & 1) f |= kC; if (bool(r & 0x80) != bool(t & 1)) f |= kV; }
      else if constexpr (K == Op::Asr) { r = u8((t >> 1) | (t & 0x80)); if (t & 1) f |= kC; if (bool(r & 0x80) != bool(t & 1)) f |= kV; }
      else if constexpr (K == Op::Asl) { r = u8(t << 1); if (t & 0x80) f |= kC; if (bool(r & 0x80) != bool(t & 0x80)) f |= kV; }
      else if constexpr (K == Op::Rol) { r = u8((t << 1) | cin); if (t & 0x80) f |= kC; if (bool(r & 0x80) != bool(t & 0x80)) f |= kV; }
      else if constexpr (K == Op::Dec) { r = u8(t - 1); f |= u8(cin); if (r == 0x7F) f |= kV; }
      else if constexpr (K == Op::Inc) { r = u8(t + 1); f |= u8(cin); if (r == 0x80) f |= kV; }
      else if constexpr (K == Op::Tst) r = t;
      else r = 0;  // Clr
      if (r & 0x80) f |= kN;
      if (!r) f |= kZ;
      c.cc_ = f;
      return r;
    }
  }
  template <Op K, unsigned R> H(h_unary_acc) {
    BEGIN;
    u8& r = acc<R>(c);
    const u8 v = unary<K>(c, r);
    if constexpr (K != Op::Tst) r = v;
    NEXT;
  }
  // CLR and TST read their operand as well (the bus cycle is there).
  template <Op K, Mode M> H(h_unary_mem) {
    BEGIN;
    const unsigned d1 = ip->d1;
    const u16 a = ea<M>(c, ip);
    const u8 v = unary<K>(c, c.rd8(a, ip->d0));
    if constexpr (K != Op::Tst) c.wr8(a, v, d1);
    NEXT;
  }
  // HD6301 AIM / OIM / EIM / TIM: the immediate in x, then a direct address or index offset.
  template <Op K, Mode M> H(h_bitop) {
    BEGIN;
    const unsigned d1 = ip->d1;
    const u8 m = ip->x;
    const u16 a = ea<M>(c, ip);
    u8 v = c.rd8(a, ip->d0);
    if constexpr (K == Op::Oim) v |= m;
    else if constexpr (K == Op::Eim) v ^= m;
    else v &= m;  // Aim, Tim
    set_nz8(c, v);
    if constexpr (K != Op::Tim) c.wr8(a, v, d1);
    NEXT;
  }
  // HC11 BSET / BCLR: the mask in x, a direct address or index offset in imm.
  template <Op K, Mode M> H(h_bset) {
    BEGIN;
    const unsigned d1 = ip->d1;
    const u16 a = ea<M>(c, ip);
    u8 v = c.rd8(a, ip->d0);
    if constexpr (K == Op::Bset) v |= ip->x;
    else v &= u8(~ip->x);
    set_nz8(c, v);
    c.wr8(a, v, d1);
    NEXT;
  }
  // HC11 BRSET / BRCLR: branch when every masked bit is set / clear. The
  // operand is in ea, the target in imm.
  template <Op K, Mode M> static u16 brset_ea(const Cpu& c, const Cell* ip) {
    if constexpr (M == Mode::Idx) return u16(c.x_ + ip->ea);
    else if constexpr (M == Mode::Idy) return u16(c.y_ + ip->ea);
    else return ip->ea;
  }
  template <Op K, Mode M> H(h_brset) {
    BEGIN;
    const u8 v = c.rd8(brset_ea<K, M>(c, ip), ip->d0);
    const u8 m = ip->x;
    const bool taken = K == Op::Brset ? (u8(~v) & m) == 0 : (v & m) == 0;
    if (taken) JUMP(ip->imm);
    NEXT;
  }

  // --- control flow ----------------------------------------------------------------------
  template <unsigned Cc> H(h_bcc) {
    BEGIN;
    if (condition<Cc>(c.cc_)) JUMP(ip->imm);
    NEXT;
  }
  template <Mode M> H(h_jmp) {
    BEGIN;
    JUMP(ea<M>(c, ip));
  }
  // JSR / BSR: the return address, low byte first. The target cell is
  // resolved after the pushes: it may switch the current page, which
  // pc_of(ip) depends on.
  static void push_return(Cpu& c, const Cell* ip, unsigned len) {
    const u16 ret = u16(c.pc_of(ip) + len);
    const unsigned d0 = ip->d0;
    c.push8(u8(ret), d0);
    c.push8(u8(ret >> 8), d0 + 1);
  }
  template <Mode M> H(h_jsr) {
    BEGIN;
    const u16 target = ea<M>(c, ip);
    push_return(c, ip, len_);
    JUMP(target);
  }
  H(h_bsr) {
    BEGIN;
    const u16 target = ip->imm;
    push_return(c, ip, len_);
    JUMP(target);
  }
  H(h_rts) {
    BEGIN;
    const unsigned d0 = ip->d0;
    const u8 h = c.pull8(d0);
    const u8 l = c.pull8(d0 + 1);
    JUMP(u16((h << 8) | l));
  }
  H(h_rti) {
    BEGIN;
    const unsigned d0 = ip->d0;
    unsigned cyc = d0;
    const u8 cc = c.pull8(cyc++);
    c.b_ = c.pull8(cyc++);
    c.a_ = c.pull8(cyc++);
    const u8 xh = c.pull8(cyc++), xl = c.pull8(cyc++);
    c.x_ = u16((xh << 8) | xl);
    if (c.family_ == Family::HC11) {
      const u8 yh = c.pull8(cyc++), yl = c.pull8(cyc++);
      c.y_ = u16((yh << 8) | yl);
    }
    const u8 ph = c.pull8(cyc), pl = c.pull8(cyc + 1);
    // A cleared I takes effect within the instruction (MC6801 RM 5.4.1.3).
    c.cc_ = c.loaded_ccr(cc);
    if (c.cc_ & kI) c.set_i();
    c.reeval_irq();
    JUMP(u16((ph << 8) | pl));
  }
  H(h_swi) {
    BEGIN;
    const unsigned d1 = ip->d1;
    c.push_state(u16(c.pc_of(ip) + 1), ip->d0);
    c.set_i();
    c.reeval_irq();
    JUMP(c.rd16(c.swi_vector_, d1));
  }
  // HD6301 TRAP: an undefined opcode, or an instruction fetched from the
  // register field; HC11: the illegal-opcode trap. The stacked PC is the
  // faulting address, so RTI retries it.
  H(h_trap) {
    BEGIN;
    const u16 pc = c.pc_of(ip);
    ++c.illegal_count_;
    c.last_illegal_pc_ = pc;
    const EntryTiming t = entry_timing(c.family_);
    c.push_state(pc, t.push);
    c.set_i();
    c.reeval_irq();
    JUMP(c.rd16(c.trap_vector_, t.vector));
  }
  H(h_illegal) {
    BEGIN;
    ++c.illegal_count_;
    c.last_illegal_pc_ = c.pc_of(ip);
    NEXT;
  }

  // --- interrupt mask, WAI, SLP ----------------------------------------------------------
  // A request is accepted two cycles after the instruction that clears I at
  // the earliest (HD6301V1 Q&A: CLI-NOP-NOP-SEI admits one, CLI-NOP-SEI does
  // not; on the MC6801 the cleared I bit is delayed one cycle and the next
  // instruction always runs). TAP keeps re-arming it, as on the MC6801.
  H(h_cli) {
    BEGIN;
    if (c.cc_ & kI) {
      c.cc_ &= u8(~kI);
      c.irq_ok_at_ = c.total_states() + cyc_ + 2;
      c.reeval_irq();
    }
    NEXT;
  }
  H(h_sei) {
    BEGIN;
    c.set_i();
    c.reeval_irq();
    NEXT;
  }
  H(h_tap) {
    BEGIN;
    c.cc_ = c.loaded_ccr(c.a_);
    if (c.cc_ & kI) c.set_i();
    else c.irq_ok_at_ = c.total_states() + cyc_ + 2;
    c.reeval_irq();
    NEXT;
  }
  H(h_wai) {
    BEGIN;
    c.push_state(u16(c.pc_of(ip) + 1), ip->d0);
    c.waiting_ = true;
    c.raise(Cpu::kPendSleep);
    NEXT;
  }
  H(h_slp) {
    BEGIN;
    c.sleeping_ = true;
    c.raise(Cpu::kPendSleep);
    NEXT;
  }
  // HC11 STOP: a NOP while S is set. Otherwise the clocks stop until XIRQ
  // or an unmasked IRQ; the oscillator start-up delay is not modelled.
  H(h_stop) {
    BEGIN;
    if (!(c.cc_ & kS)) {
      c.sleeping_ = true;
      c.raise(Cpu::kPendSleep);
    }
    NEXT;
  }

  // --- inherent ------------------------------------------------------------------------------
  H(h_nop) { BEGIN; NEXT; }
  H(h_lsrd) {
    BEGIN;
    const u16 t = c.d();
    u8 f = u8(c.cc_ & ~(kN | kZ | kV | kC));
    if (t & 1) f |= kC | kV;
    if (!(t >> 1)) f |= kZ;
    c.cc_ = f;
    c.set_d(u16(t >> 1));
    NEXT;
  }
  H(h_asld) {
    BEGIN;
    const u16 t = c.d();
    c.set_d(flags16(c, t, t, u32(t) << 1));
    NEXT;
  }
  H(h_tpa) { BEGIN; c.a_ = c.cc_; NEXT; }
  template <unsigned IR> H(h_inx) { BEGIN; u16& r = ix<IR>(c); ++r; c.cc_ = u8((c.cc_ & ~kZ) | (r ? 0 : kZ)); NEXT; }
  template <unsigned IR> H(h_dex) { BEGIN; u16& r = ix<IR>(c); --r; c.cc_ = u8((c.cc_ & ~kZ) | (r ? 0 : kZ)); NEXT; }
  H(h_clv) { BEGIN; c.cc_ &= u8(~kV); NEXT; }
  H(h_sev) { BEGIN; c.cc_ |= kV; NEXT; }
  H(h_clc) { BEGIN; c.cc_ &= u8(~kC); NEXT; }
  H(h_sec) { BEGIN; c.cc_ |= kC; NEXT; }
  H(h_sba) { BEGIN; c.a_ = sub8(c, c.a_, c.b_, 0); NEXT; }
  H(h_cba) { BEGIN; sub8(c, c.a_, c.b_, 0); NEXT; }
  H(h_aba) { BEGIN; c.a_ = add8(c, c.a_, c.b_, 0); NEXT; }
  H(h_tab) { BEGIN; c.b_ = c.a_; set_nz8(c, c.b_); NEXT; }
  H(h_tba) { BEGIN; c.a_ = c.b_; set_nz8(c, c.a_); NEXT; }
  template <unsigned IR> H(h_xgdx) {
    BEGIN;
    u16& r = ix<IR>(c);
    const u16 t = r;
    r = c.d();
    c.set_d(t);
    NEXT;
  }
  // HC11 IDIV: X = D / X, D = D % X, both unsigned. FDIV: the fraction
  // (D << 16) / X, which needs X > D. A zero divisor (or an FDIV overflow)
  // leaves $FFFF in both, as MAME has it; the manual calls them undefined.
  H(h_idiv) {
    BEGIN;
    const u16 n = c.d(), dv = c.x_;
    u8 f = u8(c.cc_ & ~(kZ | kV | kC));
    u16 q, r;
    if (!dv) { q = r = 0xFFFF; f |= kC; }
    else { q = u16(n / dv); r = u16(n % dv); }
    if (!q) f |= kZ;
    c.cc_ = f;
    c.x_ = q;
    c.set_d(r);
    NEXT;
  }
  H(h_fdiv) {
    BEGIN;
    const u16 n = c.d(), dv = c.x_;
    u8 f = u8(c.cc_ & ~(kZ | kV | kC));
    u16 q, r;
    if (!dv) { q = r = 0xFFFF; f |= kC | kV; }
    else if (dv <= n) { q = r = 0xFFFF; f |= kV; }
    else { const u32 sn = u32(n) << 16; q = u16(sn / dv); r = u16(sn % dv); }
    if (!q) f |= kZ;
    c.cc_ = f;
    c.x_ = q;
    c.set_d(r);
    NEXT;
  }
  // HD6301 $12 / $13 (undocumented): X += the byte above the stack top.
  H(h_undoc) {
    BEGIN;
    c.x_ = u16(c.x_ + c.rd8(u16(c.s_ + 1), ip->d0));
    NEXT;
  }
  // The carry is only ever set, never cleared.
  H(h_daa) {
    BEGIN;
    const unsigned a = c.a_, msn = a & 0xF0, lsn = a & 0x0F;
    unsigned cf = 0;
    if (lsn > 9 || (c.cc_ & kH)) cf |= 0x06;
    if (msn > 0x80 && lsn > 9) cf |= 0x60;
    if (msn > 0x90 || (c.cc_ & kC)) cf |= 0x60;
    const unsigned t = cf + a;
    u8 f = u8(c.cc_ & ~(kN | kZ | kV));
    if (t & 0x80) f |= kN;
    if (!(t & 0xFF)) f |= kZ;
    if (t & 0x100) f |= kC;
    c.cc_ = f;
    c.a_ = u8(t);
    NEXT;
  }
  H(h_mul) {
    BEGIN;
    const u16 t = u16(c.a_ * c.b_);
    c.set_d(t);
    c.cc_ = u8((c.cc_ & ~kC) | ((t & 0x80) ? kC : 0));
    NEXT;
  }
  template <unsigned IR> H(h_tsx) { BEGIN; ix<IR>(c) = u16(c.s_ + 1); NEXT; }
  template <unsigned IR> H(h_txs) { BEGIN; c.s_ = u16(ix<IR>(c) - 1); NEXT; }
  H(h_ins) { BEGIN; ++c.s_; NEXT; }
  H(h_des) { BEGIN; --c.s_; NEXT; }
  template <unsigned IR> H(h_abx) { BEGIN; ix<IR>(c) = u16(ix<IR>(c) + c.b_); NEXT; }
  template <unsigned R> H(h_psh) { BEGIN; c.push8(acc<R>(c), ip->d0); NEXT; }
  template <unsigned R> H(h_pul) { BEGIN; acc<R>(c) = c.pull8(ip->d0); NEXT; }
  template <unsigned IR> H(h_pshx) {
    BEGIN;
    const unsigned d0 = ip->d0;
    const u16 r = ix<IR>(c);
    c.push8(u8(r), d0);
    c.push8(u8(r >> 8), d0 + 1);
    NEXT;
  }
  template <unsigned IR> H(h_pulx) {
    BEGIN;
    const unsigned d0 = ip->d0;
    const u8 h = c.pull8(d0);
    const u8 l = c.pull8(d0 + 1);
    ix<IR>(c) = u16((h << 8) | l);
    NEXT;
  }

  // ---------------------------------------------------------------------------
  // Handler selection

  template <template <Mode> class T>
  static Handler by_mode(Mode m) {
    switch (m) {
      case Mode::Imm: return T<Mode::Imm>::fn();
      case Mode::Dir: return T<Mode::Dir>::fn();
      case Mode::Idx: return T<Mode::Idx>::fn();
      case Mode::Idy: return T<Mode::Idy>::fn();
      default: return T<Mode::Ext>::fn();
    }
  }
  template <Op K, unsigned R> struct Alu8 { template <Mode M> struct At { static Handler fn() { return &h_alu8<K, R, M>; } }; };
  template <unsigned R> struct Sta { template <Mode M> struct At { static Handler fn() { return &h_sta<R, M>; } }; };
  template <Op K, unsigned IR> struct Alu16 { template <Mode M> struct At { static Handler fn() { return &h_alu16<K, IR, M>; } }; };
  template <Op K, unsigned IR> struct St16 { template <Mode M> struct At { static Handler fn() { return &h_st16<K, IR, M>; } }; };
  template <Op K> struct Bset { template <Mode M> struct At { static Handler fn() { return &h_bset<K, M>; } }; };
  template <Op K> struct Brset { template <Mode M> struct At { static Handler fn() { return &h_brset<K, M>; } }; };
  template <Op K> struct UnaryMem { template <Mode M> struct At { static Handler fn() { return &h_unary_mem<K, M>; } }; };
  template <Op K> struct BitOp { template <Mode M> struct At { static Handler fn() { return &h_bitop<K, M>; } }; };
  template <Mode M> struct Jsr { static Handler fn() { return &h_jsr<M>; } };
  template <Mode M> struct Jmp { static Handler fn() { return &h_jmp<M>; } };

  template <Op K> static Handler sel_alu8(const DecodedInsn& d) {
    return d.reg ? by_mode<Alu8<K, 1>::template At>(d.mode) : by_mode<Alu8<K, 0>::template At>(d.mode);
  }
  template <Op K> static Handler sel_unary(const DecodedInsn& d) {
    if (d.mode == Mode::Inh) return d.reg ? &h_unary_acc<K, 1> : &h_unary_acc<K, 0>;
    return by_mode<UnaryMem<K>::template At>(d.mode);
  }
  template <Op K> static Handler sel_alu16(const DecodedInsn& d) {
    return d.reg ? by_mode<Alu16<K, 1>::template At>(d.mode) : by_mode<Alu16<K, 0>::template At>(d.mode);
  }
  template <Op K> static Handler sel_st16(const DecodedInsn& d) {
    return d.reg ? by_mode<St16<K, 1>::template At>(d.mode) : by_mode<St16<K, 0>::template At>(d.mode);
  }
  // The X instructions with the index register in d.reg.
  template <template <unsigned> class T> static Handler by_ir(const DecodedInsn& d) {
    return d.reg ? T<1>::fn() : T<0>::fn();
  }
  template <unsigned IR> struct Inx { static Handler fn() { return &h_inx<IR>; } };
  template <unsigned IR> struct Dex { static Handler fn() { return &h_dex<IR>; } };
  template <unsigned IR> struct Xgdx { static Handler fn() { return &h_xgdx<IR>; } };
  template <unsigned IR> struct Tsx { static Handler fn() { return &h_tsx<IR>; } };
  template <unsigned IR> struct Txs { static Handler fn() { return &h_txs<IR>; } };
  template <unsigned IR> struct Abx { static Handler fn() { return &h_abx<IR>; } };
  template <unsigned IR> struct Pshx { static Handler fn() { return &h_pshx<IR>; } };
  template <unsigned IR> struct Pulx { static Handler fn() { return &h_pulx<IR>; } };
  template <std::size_t... I>
  static constexpr std::array<Handler, 16> bcc_table(std::index_sequence<I...>) {
    return {&h_bcc<unsigned(I)>...};
  }

  static Handler select(const DecodedInsn& d) {
    static constexpr std::array<Handler, 16> bcc = bcc_table(std::make_index_sequence<16>{});
    switch (d.op) {
      case Op::Illegal: return &h_illegal;
      case Op::Trap: return &h_trap;
      case Op::Nop: return &h_nop;
      case Op::Lsrd: return &h_lsrd;
      case Op::Asld: return &h_asld;
      case Op::Tap: return &h_tap;
      case Op::Tpa: return &h_tpa;
      case Op::Inx: return by_ir<Inx>(d);
      case Op::Dex: return by_ir<Dex>(d);
      case Op::Clv: return &h_clv;
      case Op::Sev: return &h_sev;
      case Op::Clc: return &h_clc;
      case Op::Sec: return &h_sec;
      case Op::Cli: return &h_cli;
      case Op::Sei: return &h_sei;
      case Op::Sba: return &h_sba;
      case Op::Cba: return &h_cba;
      case Op::Undoc: return &h_undoc;
      case Op::Tab: return &h_tab;
      case Op::Tba: return &h_tba;
      case Op::Xgdx: return by_ir<Xgdx>(d);
      case Op::Daa: return &h_daa;
      case Op::Slp: return &h_slp;
      case Op::Aba: return &h_aba;
      case Op::Bcc: return bcc[d.cond & 15];
      case Op::Tsx: return by_ir<Tsx>(d);
      case Op::Ins: return &h_ins;
      case Op::Pul: return d.reg ? &h_pul<1> : &h_pul<0>;
      case Op::Des: return &h_des;
      case Op::Txs: return by_ir<Txs>(d);
      case Op::Psh: return d.reg ? &h_psh<1> : &h_psh<0>;
      case Op::Pulx: return by_ir<Pulx>(d);
      case Op::Rts: return &h_rts;
      case Op::Abx: return by_ir<Abx>(d);
      case Op::Rti: return &h_rti;
      case Op::Pshx: return by_ir<Pshx>(d);
      case Op::Mul: return &h_mul;
      case Op::Wai: return &h_wai;
      case Op::Swi: return &h_swi;
      case Op::Neg: return sel_unary<Op::Neg>(d);
      case Op::Com: return sel_unary<Op::Com>(d);
      case Op::Lsr: return sel_unary<Op::Lsr>(d);
      case Op::Ror: return sel_unary<Op::Ror>(d);
      case Op::Asr: return sel_unary<Op::Asr>(d);
      case Op::Asl: return sel_unary<Op::Asl>(d);
      case Op::Rol: return sel_unary<Op::Rol>(d);
      case Op::Dec: return sel_unary<Op::Dec>(d);
      case Op::Inc: return sel_unary<Op::Inc>(d);
      case Op::Tst: return sel_unary<Op::Tst>(d);
      case Op::Clr: return sel_unary<Op::Clr>(d);
      case Op::Aim: return by_mode<BitOp<Op::Aim>::template At>(d.mode);
      case Op::Oim: return by_mode<BitOp<Op::Oim>::template At>(d.mode);
      case Op::Eim: return by_mode<BitOp<Op::Eim>::template At>(d.mode);
      case Op::Tim: return by_mode<BitOp<Op::Tim>::template At>(d.mode);
      case Op::Jmp: return by_mode<Jmp>(d.mode);
      case Op::Jsr: return by_mode<Jsr>(d.mode);
      case Op::Bsr: return &h_bsr;
      case Op::Sub: return sel_alu8<Op::Sub>(d);
      case Op::Cmp: return sel_alu8<Op::Cmp>(d);
      case Op::Sbc: return sel_alu8<Op::Sbc>(d);
      case Op::And: return sel_alu8<Op::And>(d);
      case Op::Bit: return sel_alu8<Op::Bit>(d);
      case Op::Lda: return sel_alu8<Op::Lda>(d);
      case Op::Sta: return d.reg ? by_mode<Sta<1>::template At>(d.mode) : by_mode<Sta<0>::template At>(d.mode);
      case Op::Eor: return sel_alu8<Op::Eor>(d);
      case Op::Adc: return sel_alu8<Op::Adc>(d);
      case Op::Ora: return sel_alu8<Op::Ora>(d);
      case Op::Add: return sel_alu8<Op::Add>(d);
      case Op::Subd: return by_mode<Alu16<Op::Subd, 0>::template At>(d.mode);
      case Op::Addd: return by_mode<Alu16<Op::Addd, 0>::template At>(d.mode);
      case Op::Cpd: return by_mode<Alu16<Op::Cpd, 0>::template At>(d.mode);
      case Op::Cpx: return sel_alu16<Op::Cpx>(d);
      case Op::Ldd: return by_mode<Alu16<Op::Ldd, 0>::template At>(d.mode);
      case Op::Ldx: return sel_alu16<Op::Ldx>(d);
      case Op::Lds: return by_mode<Alu16<Op::Lds, 0>::template At>(d.mode);
      case Op::Std: return by_mode<St16<Op::Std, 0>::template At>(d.mode);
      case Op::Stx: return sel_st16<Op::Stx>(d);
      case Op::Sts: return by_mode<St16<Op::Sts, 0>::template At>(d.mode);
      case Op::Idiv: return &h_idiv;
      case Op::Fdiv: return &h_fdiv;
      case Op::Bset: return by_mode<Bset<Op::Bset>::template At>(d.mode);
      case Op::Bclr: return by_mode<Bset<Op::Bclr>::template At>(d.mode);
      case Op::Brset: return by_mode<Brset<Op::Brset>::template At>(d.mode);
      case Op::Brclr: return by_mode<Brset<Op::Brclr>::template At>(d.mode);
      case Op::Stop: return &h_stop;
    }
    return &h_illegal;
  }

  static const Cell* cross(Cpu& c, const Cell* ip) {
    const Cell* next = c.cells_for(c.pc_of(ip));
    EMU_GOTO(c, next);
  }

  static const Cell* fill(Cpu& c, const Cell* ip) {
    const u16 pc = c.pc_of(ip);
    Cell cell{};
    if (pc >= c.trap_lo_ && pc <= c.trap_hi_) {
      // HD6301 address error: the fetch itself traps.
      cell.fn = &h_trap;
      cell.len = 1;
      cell.cyc = entry_timing(c.family_).cycles;
    } else {
      const DecodedInsn d = c.decode_at(pc);
      cell.fn = select(d);
      cell.imm = d.imm;
      cell.len = d.len;
      cell.cyc = d.cyc;
      cell.d0 = d.d0;
      cell.d1 = d.d1;
      cell.x = d.x;
      cell.ea = d.ea;
    }
    // RAM lines holding code become write-slow so writes can invalidate;
    // code behind a device line is invalidated by code_written().
    if (c.bus_.attr(pc) & Bus::kRdSlow) c.device_code_ = true;
    c.bus_.mark_code(pc);
    c.bus_.mark_code(u16(pc + cell.len - 1));
    *const_cast<Cell*>(ip) = cell;
    EMU_GOTO(c, ip);
  }
#undef H
#undef BEGIN
#undef NEXT
#undef JUMP
};

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::fill(cpu, ip)); }
const Cell* cell_cross(Cpu& cpu, const Cell* ip) { EMU_TAILCALL(ExecImpl::cross(cpu, ip)); }
}  // namespace detail

}  // namespace m6801
