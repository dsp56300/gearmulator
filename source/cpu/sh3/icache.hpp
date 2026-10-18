// Pre-decoded instruction cells: the family's layout (cpu/sh/cell.hpp) with
// this core's handlers.
#pragma once
#include "cpu/sh/cell.hpp"
#include "cpu/sh3/types.hpp"

namespace sh3 {

class Cpu;
using Cell = sh::Cell<Cpu>;
using Handler = Cell::Handler;
using CellPages = sh::CellPages<Cpu>;
static_assert(sizeof(Cell) == 32);

using sh::kFlagDirect;
using sh::kFlagPageEdge;

using sh::kCellsPerPage;
using sh::kPageShift;
using sh::kPageSize;

}  // namespace sh3
