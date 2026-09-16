// Pre-decoded instruction cells: one 16-byte Cell per guest code byte.
#pragma once
#include "common/cells.hpp"
#include "cpu/mcs96/types.hpp"

namespace mcs96 {

class Cpu;
struct Cell;
using Handler = const Cell* (*)(Cpu&, const Cell*);

struct Cell {
  Handler fn;  // execution handler (or the fill handler while unpopulated)
  u16 imm;     // immediate / displacement / indexed offset
  u8 a;        // source operand: register address, pointer register, or count
  u8 b;        // destination register / second operand
  u8 x;        // third register (3-operand result), bit number, condition, mode byte
  u8 len;      // instruction length in bytes (prefix included)
  u8 cyc;      // states, operand in the register file (or fixed)
  u8 cyc2;     // states, operand through the memory controller / branch taken / stack external
};
static_assert(sizeof(Cell) == 16, "Cell must stay 16 bytes: one per guest code byte");

// 2 KiB pages with guard cells that resolve sequential flow across page boundaries.
using CellPages = emu::CellPages<Cell, 11, 0>;
constexpr unsigned kGuardCells = 8;  // longest instruction: prefix + opcode + 6 operand bytes

namespace detail {
const Cell* cell_fill(Cpu& cpu, const Cell* ip);
const Cell* cell_cross(Cpu& cpu, const Cell* ip);
}

}  // namespace mcs96
