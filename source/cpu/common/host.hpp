// What the cores need to know about the host they are compiled for.
//
// The cached threaded-code cores were written on 64-bit clang, and two of their assumptions are
// about the host rather than about the guest. They are settled here, once, so that a new target
// (a 32-bit x86 or ARM, RISC-V, WebAssembly, a compiler other than clang) is a matter of this file
// and not of every core.
#pragma once
#include <cstdint>

// ---- Guaranteed tail calls -------------------------------------------------------------------
//
// Handlers chain with a guaranteed tail call where the host has one; elsewhere the run loop
// dispatches and a handler simply returns the next cell. Having the attribute is not the same as
// the target honouring it: clang accepts [[clang::musttail]] everywhere but fails in the back end
// on targets that cannot guarantee the call. Those are listed here.
//   * WebAssembly only guarantees tail calls with the tail-call feature (-mtail-call), which the
//     embedder has to opt into because it raises the minimum runtime version.
// EMU_FORCE_NO_MUSTTAIL picks the run-loop dispatch regardless, e.g. to test it on a host that
// normally tail-calls.
#if defined(EMU_FORCE_NO_MUSTTAIL)
#define EMU_HAS_MUSTTAIL 0
#elif !defined(__clang__) || !defined(__has_cpp_attribute)
#define EMU_HAS_MUSTTAIL 0
#elif !__has_cpp_attribute(clang::musttail)
#define EMU_HAS_MUSTTAIL 0
#elif defined(__wasm__) && !defined(__wasm_tail_call__)
#define EMU_HAS_MUSTTAIL 0
#else
#define EMU_HAS_MUSTTAIL 1
#endif

#if EMU_HAS_MUSTTAIL
#define EMU_TAILCALL(expr) [[clang::musttail]] return (expr)
#else
#define EMU_TAILCALL(expr) return (expr)
#endif

// ---- Cell stride -----------------------------------------------------------------------------
//
// A Cell is laid out around 8-byte handler pointers and sized to a power of two, so that the
// program counter implied by a cell pointer (`ip - page`) is a shift. With 4-byte pointers the
// same fields would pack into 12 or 24 bytes; the pad below puts the stride back. It goes last in
// the Cell, one per handler pointer, and is empty on a 64-bit host:
//
//   struct Cell {
//     Handler fn;
//     ...
//     EMU_CELL_POINTER_PAD(1)
//   };
//
// Cells are value-initialized and then filled in by field, so the extra member is never named.
#if UINTPTR_MAX == 0xffffffffu
#define EMU_HOST_POINTER_BYTES 4
#define EMU_CELL_POINTER_PAD(handlers) ::std::uint32_t pointer_pad_[handlers] = {};
#elif UINTPTR_MAX == 0xffffffffffffffffu
#define EMU_HOST_POINTER_BYTES 8
#define EMU_CELL_POINTER_PAD(handlers)
#else
#error "cpu/common/host.hpp: unexpected pointer width"
#endif
