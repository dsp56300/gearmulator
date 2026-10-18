// Pre-decoded instruction cells: one 16-byte Cell per guest code word
// (instructions are word-aligned and 2 to 10 bytes long).
#pragma once
#include "common/cells.hpp"
#include "cpu/h8300h/types.hpp"

namespace h8300h {

class Cpu;
struct Cell;
using Handler = const Cell* (*)(Cpu&, const Cell*);

struct Cell {
  Handler fn;  // execution handler (or the fill handler while unpopulated)
  u32 imm;     // immediate / absolute address / displacement / relative target
  u8 r;        // source register (bits 7-4) | destination register (bits 3-0), as encoded
  u8 len;      // instruction length in bytes
  u8 cyc;      // static states: instruction fetch words + internal operation
  u8 x;        // bit number / condition / operand size / direction, per handler
};
static_assert(sizeof(Cell) == 16, "Cell must stay 16 bytes: one per guest code word");

// 4 KiB pages (2048 cells) over the 16 MiB space; the guard covers the
// longest instruction (five words).
using CellPages = emu::CellPages<Cell, 12, 1>;
constexpr unsigned kGuardCells = 5;

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip);
const Cell* cell_cross(Cpu& cpu, const Cell* ip);
}

}  // namespace h8300h
