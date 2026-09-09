// Pre-decoded instruction cells.
//
// One 32-byte Cell per instruction word (2 guest bytes) in a 64 KB page.  A
// cell holds the handler for the instruction (`fn`) and its decoded operands.
// A delayed branch also carries its delay slot: the slot's decoded operands
// and, in `real`, the handler that executes it, so the pair runs as one chain
// (branch prelude -> slot -> completion) without touching the slot's own cell.
// Cells start out pointing at a fill handler that decodes the word on first
// execution.
#pragma once
#include "cpu/sh2/types.hpp"

namespace sh2 {

class Cpu;
struct Cell;
using Handler = const Cell* (*)(Cpu&, const Cell*);

struct Cell {
  Handler fn;      // executes the instruction, then chains to the next cell
  Handler real;    // delayed branch: the slot's handler, run after the prelude
  s32 imm;         // immediate / displacement (pre-scaled); cell delta when kFlagDirect
  u8 n, m;         // register fields
  u8 fetch;        // static external fetch states charged with this cell (line start, cache off)
  u8 flags;        // bits 0-3: bus access class of the code line; 4-7: kFlag*
  s32 slot_imm;    // delayed branch: decoded delay slot
  u8 slot_n, slot_m;
  u8 cyc, slot_cyc;  // base execution states of the instruction / its slot (tables 2.12-2.17)
};
static_assert(sizeof(Cell) == 32);

constexpr u8 kFlagDirect = 0x10;      // imm is the target's cell delta (PC-relative branch inside the page)
constexpr u8 kFlagTargetLine = 0x20;  // the direct target is the second word of an external fetch line
constexpr u8 kFlagPageEdge = 0x40;    // delayed branch whose fall-through leaves the page

constexpr unsigned kPageShift = 16;
constexpr u32 kPageSize = 1u << kPageShift;
constexpr u32 kCellsPerPage = kPageSize / 2;

}  // namespace sh2
