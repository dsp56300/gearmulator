// Fixed-width integer aliases shared by every emulator core.
#pragma once
#include <cstddef>
#include <cstdint>

namespace emu {
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
}  // namespace emu
