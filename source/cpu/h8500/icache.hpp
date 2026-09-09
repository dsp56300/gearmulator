// Instruction cache: pre-decoded "threaded code".
//
// Every guest code byte has a 16-byte Cell in a per-page array (64 K cells per
// 64 KB page, allocated on first execution).  A cell whose address is the start
// of an instruction holds a pointer to the handler that executes it plus every
// parameter the handler needs, including the precomputed state counts.  Cells
// that have never been executed point at a fill handler which decodes the
// instruction through the bus, writes the cell and tail-calls the real handler,
// so there is a single execution path: the CPU only ever runs from cells.
//
// Handlers chain with a guaranteed tail call.  The program counter is implicit
// in the cell pointer (pc = ip - page_base); the next sequential instruction is
// `ip + len`, a branch target inside the page is `page_base + target`.
#pragma once
#include "cpu/h8500/types.hpp"

namespace h8500 {

class Cpu;
struct Cell;
using Handler = const Cell* (*)(Cpu&, const Cell*);

struct Cell {
  Handler fn;   // execution handler (or the fill handler while unpopulated)
  u16 imm;      // immediate / displacement / absolute address / register list
  u8 cyc;       // states, primary outcome (branch not taken, no exception), static part
  u8 cyc2;      // states, alternate outcome (branch taken, zero divide, trap taken)
  u8 r;         // EA register (bits 7-4) | OP register / CR / cc / vector (bits 3-0); page for @aa:24
  u8 x;         // instruction length (bits 2-0) | bit number / SCB condition (bits 7-4)
  u8 icnt;      // I: operand bytes accessed in memory (primary outcome), for the bus-class penalty
  u8 icnt2;     // I for the alternate outcome
};
static_assert(sizeof(Cell) == 16, "Cell must stay 16 bytes: one per guest code byte");

constexpr unsigned kCellsPerPage = 0x10000;

namespace detail {
// Handler installed in every cell of a freshly allocated page.
const Cell* cell_fill(Cpu& cpu, const Cell* ip);
}  // namespace detail

}  // namespace h8500
