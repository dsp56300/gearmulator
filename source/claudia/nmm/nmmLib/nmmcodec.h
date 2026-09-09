#pragma once
#include <cstdint>
#include <cstring>

namespace nmm
{
    // Quantize the ADC's signed 16-bit result into a left-justified DSP word.
    inline uint32_t encodeAdc(float sample)
    {
        // Decode IEEE-754 bits directly: fast-math may remove floating-point
        // NaN/Inf checks. Integer conversion also defines truncation and clipping.
        static_assert(sizeof(float)==sizeof(uint32_t));
        uint32_t bits;std::memcpy(&bits,&sample,sizeof bits);
        const auto exponent=(bits>>23)&255u;
        if(exponent==255 || exponent<112) return 0;
        const bool negative=(bits>>31)!=0;
        const auto magnitude=exponent>=127?(negative?32768u:32767u):((bits&0x7fffffu)|0x800000u)>>(135-exponent);
        return ((negative?0u-magnitude:magnitude)&0xffffu)<<8;
    }
	// AD1865: the last 18 serial bits before the latch edge form a signed,
	// two's-complement sample. The OS volume table already scales the 24-bit
	// mixer into this right-justified range; this is decoding, not makeup gain.
	constexpr float decodeDac(uint32_t serialWord)
	{
		const auto bits=serialWord & 0x3ffffu;
		const auto value=static_cast<int32_t>(bits ^ 0x20000u)-0x20000;
		return static_cast<float>(value)/131072.0f;
	}
}
