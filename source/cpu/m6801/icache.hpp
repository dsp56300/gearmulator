// Pre-decoded instruction cells: one 16-byte Cell per guest code byte.
//
// A cell holds the handler and everything it needs: the operand (immediate
// value, direct / extended address, index offset or branch target), the
// length, the E cycles of the instruction and the cycles within it at which
// its data accesses happen (so a register access sees the time of its own
// bus cycle, see Cpu::rd8).
#pragma once
#include "common/cells.hpp"
#include "common/host.hpp"
#include "cpu/m6801/types.hpp"

namespace m6801 {

class Cpu;
struct Cell;
using Handler = const Cell* (*)(Cpu&, const Cell*);

struct Cell {
  Handler fn;  // execution handler (or the fill handler while unpopulated)
  u16 imm;     // operand: immediate, address, index offset or branch target
  u8 len;      // instruction length in bytes
  u8 cyc;      // E cycles
  u8 d0;       // cycle of the first data access (reads; stack pushes / pulls)
  u8 d1;       // cycle of the write of a read-modify-write, of an SWI vector fetch
  u8 x;        // AIM/OIM/EIM/TIM immediate, BSET/BCLR/BRSET/BRCLR mask
  u8 ea;       // BRSET/BRCLR direct address or index offset
  EMU_CELL_POINTER_PAD(1)
};
static_assert(sizeof(Cell) == 16, "Cell must stay 16 bytes: one per guest code byte");

// 4 KB pages (a banked code window is page-aligned); the guard covers the
// longest instruction (five bytes, an HC11 BRSET on page $18) and wraps a
// page end, $FFFF included.
using CellPages = emu::CellPages<Cell, 12, 0>;
constexpr unsigned kGuardCells = 5;

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip);
const Cell* cell_cross(Cpu& cpu, const Cell* ip);
}  // namespace detail

}  // namespace m6801
