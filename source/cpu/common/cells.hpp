// Storage for pre-decoded instruction cells ("threaded code").
//
// Every guest code address that can start an instruction owns a Cell in a
// per-page array, allocated on first execution.  A cell holds the handler
// that executes the instruction plus every parameter it needs; cells that
// have never been executed hold the fill handler.  This template owns the
// pages and the invalidation bookkeeping; the Cell layout and the handlers are
// the core's own.
//
// Granule: bytes of guest code per cell (1 for byte-coded ISAs, 2 for
// word-coded ones).  PageShift: log2 of the guest bytes one page covers.
//
// Guard cells: a page may carry `guard` extra cells past its end, holding a
// core-supplied "cross" cell whose handler re-resolves the address in the
// next page.  Sequential flow then never checks for the page end; the guard
// count must cover the longest instruction.
//
// Banked code windows: a board that pages different contents into the same
// address range (a ROM bank window) tells the container which bank is in
// place; each bank keeps its own decoded cells and a switch moves page
// pointers only.
//
#pragma once
#include <algorithm>
#include <cassert>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "common/types.hpp"

namespace emu {

template <class Cell, unsigned PageShift, unsigned GranuleShift = 0>
class CellPages {
 public:
  static constexpr unsigned kPageShift = PageShift;
  static constexpr u32 kPageSize = 1u << PageShift;
  static constexpr u32 kCellsPerPage = kPageSize >> GranuleShift;

  CellPages(unsigned addr_bits, const Cell& blank, unsigned guard = 0, const Cell& cross = Cell{})
      : pages_(size_t(1) << (addr_bits - PageShift)), blank_(blank), cross_(cross), guard_(guard) {}

  // Cells of the page containing guest address `a`, allocating on first use.
  Cell* page(u32 a) {
    const size_t idx = a >> PageShift;
    Cell* cells = pages_[idx].get();
    if (!cells) cells = alloc(idx);
    return cells;
  }
  Cell* page_if_allocated(u32 a) const { return pages_[a >> PageShift].get(); }
  static u32 page_base(u32 a) { return a & ~(kPageSize - 1); }
  static u32 index_in_page(u32 a) { return (a & (kPageSize - 1)) >> GranuleShift; }

  void invalidate_all() {
    for (auto& p : pages_) if (p) std::fill_n(p.get(), kCellsPerPage, blank_);
  }
  // Drop cells covering [addr, addr+len); `back` is the number of granules an
  // instruction may start before `addr` and still extend into the range.
  void invalidate_range(u32 addr, u32 len, u32 back) {
    // Instructions starting in the previous page may extend into the range.
    if (back && index_in_page(addr) < back && addr >= back) invalidate_range(addr - back, back, 0);
    const u32 end = addr + len;
    u32 a = addr;
    while (a < end) {
      const u32 pb = page_base(a);
      Cell* cells = pages_[a >> PageShift].get();
      const u32 chunk = std::min<u32>(end - a, pb + kPageSize - a);
      if (cells) {
        u32 first = index_in_page(a);
        const u32 count = (chunk + (1u << GranuleShift) - 1) >> GranuleShift;
        const u32 b = std::min<u32>(first, back);
        first -= b;
        std::fill_n(cells + first, std::min<u32>(count + b, kCellsPerPage - first), blank_);
      }
      a = pb + kPageSize;
    }
  }
  const Cell& blank() const { return blank_; }

  // Put bank `key` in place in the window [base, base+size) (page-aligned;
  // windows must not overlap).  The first call for a window adopts the cells
  // already there as that key's.  Returns whether the selection changed.
  bool select_bank(u32 base, u32 size, u32 key) {
    assert((base & (kPageSize - 1)) == 0 && (size & (kPageSize - 1)) == 0 && size != 0);
    Window* w = nullptr;
    for (auto& x : windows_) if (x.base == base && x.size == size) w = &x;
    if (!w) {
      windows_.push_back(Window{base, size, key, {}});
      return false;
    }
    if (w->current == key) return false;
    const size_t first = base >> PageShift, count = size >> PageShift;
    auto& out = w->banks[w->current];
    out.resize(count);
    auto& in = w->banks[key];
    in.resize(count);
    for (size_t i = 0; i < count; ++i) {
      out[i] = std::move(pages_[first + i]);
      pages_[first + i] = std::move(in[i]);
    }
    w->current = key;
    return true;
  }

 private:
  struct Window {
    u32 base, size, current;
    std::unordered_map<u32, std::vector<std::unique_ptr<Cell[]>>> banks;  // the banks not in place
  };

  Cell* alloc(size_t idx) {
    pages_[idx].reset(new Cell[kCellsPerPage + guard_]);
    std::fill_n(pages_[idx].get(), kCellsPerPage, blank_);
    std::fill_n(pages_[idx].get() + kCellsPerPage, guard_, cross_);
    return pages_[idx].get();
  }

  std::vector<std::unique_ptr<Cell[]>> pages_;
  // deque, not vector: Window holds an unordered_map of move-only vectors, so its move
  // constructor is not noexcept while the map still *declares* a copy constructor.  A
  // vector reallocation therefore instantiates that copy, which fails on unique_ptr
  // (MSVC rejects it; libstdc++ and libc++ never got that far).  A deque does not
  // relocate the elements it already holds, which also keeps the Window* above stable.
  std::deque<Window> windows_;
  Cell blank_;
  Cell cross_;
  unsigned guard_;
};

}  // namespace emu
