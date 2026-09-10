#pragma once

// Portable board-oracle helpers for the isolated Filter F/092 differential.
// This header intentionally has no emulator or desktop dependencies. The
// generated build/fixtures/filter_f92_vectors.h contains records produced by
// nmmFilterF92Differential --emit-board; a Teensy test can use initialize() to
// recreate each X/Y state and hashState() to check the complete result.

#include <cstddef>
#include <cstdint>

namespace nmm::native::filter_f92_oracle
{
constexpr std::size_t kWords=0x800;
constexpr uint32_t kBoundary[]={
    0x000000,0x000001,0x000002,0x007fff,0x400000,0x7ffffe,
    0x7fffff,0x800000,0x800001,0xbfffff,0xffffff,0xfffffe,
    0xff0000,0x010000,0x008000,0x004000};

constexpr uint32_t nextWord(uint32_t& state) noexcept
{
    state=state*1664525u+1013904223u;
    return (state>>8)&0xffffffu;
}

inline void initialize(uint32_t vector,uint32_t seed,uint32_t* x,uint32_t* y) noexcept
{
    uint32_t state=seed;
    for(std::size_t i=0;i<kWords;++i)
    {
        x[i]=i<16?kBoundary[(i+vector)%16]:nextWord(state);
        y[i]=i<16?kBoundary[(i+vector*3)%16]:nextWord(state);
    }
}

constexpr uint64_t mixByte(uint64_t hash,uint8_t byte) noexcept
{
    return (hash^byte)*1099511628211ull;
}

constexpr uint64_t mixWord(uint64_t hash,uint32_t word) noexcept
{
    hash=mixByte(hash,static_cast<uint8_t>(word));
    hash=mixByte(hash,static_cast<uint8_t>(word>>8));
    hash=mixByte(hash,static_cast<uint8_t>(word>>16));
    hash=mixByte(hash,static_cast<uint8_t>(word>>24));
    return hash;
}

constexpr uint64_t mixDWord(uint64_t hash,uint64_t word) noexcept
{
    for(unsigned i=0;i<8;++i) hash=mixByte(hash,static_cast<uint8_t>(word>>(8*i)));
    return hash;
}

inline uint64_t hashState(const uint32_t* x,const uint32_t* y,
                          uint32_t r3,uint32_t r4,int64_t a,int64_t b,
                          uint32_t x0,uint32_t x1,uint32_t y0,uint32_t y1) noexcept
{
    auto hash=1469598103934665603ull;
    for(std::size_t i=0;i<kWords;++i) hash=mixWord(hash,x[i]&0xffffffu);
    for(std::size_t i=0;i<kWords;++i) hash=mixWord(hash,y[i]&0xffffffu);
    hash=mixWord(hash,r3);hash=mixWord(hash,r4);
    hash=mixDWord(hash,static_cast<uint64_t>(a));
    hash=mixDWord(hash,static_cast<uint64_t>(b));
    hash=mixWord(hash,x0);hash=mixWord(hash,x1);hash=mixWord(hash,y0);hash=mixWord(hash,y1);
    return hash;
}
}
