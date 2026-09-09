#pragma once

/*
 * gpLib — Roland GP tone generator (Toshiba TC24SC201AF-002 / TC6116AF):
 * 28-voice DPCM sample player with a state-variable filter, three envelopes
 * per voice and a ROM-programmed reverb/chorus section, as used by the SC-55 family.
 *
 * Derived from Nuked-SC55's src/pcm.cpp, which carries:
 *
 * Copyright (C) 2021, 2024 nukeykt
 *
 * This file is part of Nuked-SC55.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  Thanks:
 *      John McMaster (https://siliconprawn.org):
 *          PCM chip decap
 */

#include "gp.h"
#include "../pcmInterpolation.h"

namespace gpLib::gpInternal
{
	// ---- register names (the register map) ------------------------------

	// RAM1, 20-bit words, per voice.
	enum Ram1 : uint32_t
	{
		AddrEnd = 0,		// sample end address
		TvfBandpass = 1,	// SVF bandpass integrator
		AddrLoop = 2,		// loop / key-on reload address
		TvfLowpass = 3,		// SVF lowpass integrator, default output tap
		AddrCurrent = 4,	// play pointer
		DpcmAccum = 5,		// DPCM accumulator
	};

	// RAM2, 16-bit words, per voice.
	enum Ram2 : uint32_t
	{
		Pitch = 0,
		Pan = 1,			// [15:8] left, [7:0] right, both signed
		Sends = 2,			// [15:8] -> send sum 0, [7:0] -> send sum 1
		TvVolume1 = 3,		// envelope control words: [7:0] rate/shape, [15:8] target
		TvVolume2 = 4,
		TvCutoff = 5,
		Control = 6,		// b0 IRQ enable, b1 output tap, [14:8] filter damping
		AddrControl = 7,	// [4:0] pitch source, b5 okey, b6 alt loop, b7 reverse, [11:8] bank, [15:12] cached nibble
		Phase = 8,			// [13:0] sub-phase, b14 IRQ fired, b15 loop direction
		TvVolume1Level = 9,
		TvVolume2Level = 10,
		TvCutoffLevel = 11,	// doubles as the filter coefficient
	};

	// The channels the effects engine owns.
	constexpr uint32_t FxA = 28;	// microprogram temporaries / ERAM bases
	constexpr uint32_t FxB = 29;
	constexpr uint32_t Dac = 30;	// DAC stage, effect coefficients, LFSR
	constexpr uint32_t Lfo = 31;	// chorus LFO pseudo-voice, mix accumulators

	// Channel 31 RAM1 slots 1 and 3 are the running mix sums.
	constexpr uint32_t MixL = 1;
	constexpr uint32_t MixR = 3;

	// ---- the chip's arithmetic ------------------------------------------

	// 20-bit saturating add with carry-in.
	inline uint32_t addclip20(const uint32_t add1, const uint32_t add2, const uint32_t cin)
	{
		const uint32_t sum = (add1 + add2 + cin) & 0xfffff;
		// Two negatives giving a positive, or two positives giving a
		// negative, clamp to the nearest rail.
		const uint32_t negativeOverflow = add1 & add2 & ~sum & 0x80000;
		const uint32_t positiveOverflow = ~add1 & ~add2 & sum & 0x80000;
		return negativeOverflow ? 0x80000 : positiveOverflow ? 0x7ffff : sum;
	}

	inline int32_t sign20(const uint32_t v)
	{
		return static_cast<int32_t>(v << 12) >> 12;
	}

	// 20 x 8 bit signed multiply. The product keeps its low 25 bits and
	// takes its sign from bit 27, which is what the silicon does with the
	// two bits in between.
	inline int32_t multi(const uint32_t val1, const int8_t val2)
	{
		const int32_t product = sign20(val1) * val2;
		// All ones when bit 27 is set.
		const int32_t sign = static_cast<int32_t>(static_cast<uint32_t>(product) << 4) >> 31;
		return (product & 0x1ffffff) | (sign & ~0x1ffffff);
	}

	inline int32_t multi(const int32_t val1, const int8_t val2)
	{
		return multi(static_cast<uint32_t>(val1), val2);
	}

	// Arithmetic right shift of a value that may be negative.
	inline int32_t sar(const int32_t v, const uint32_t shift)
	{
		return v >> shift;
	}

	inline constexpr auto& interp_lut = pcmInterpolation::interpolationCoefficients;

	// The four bits of the envelope clock, reversed.
	inline uint32_t bitReverse4(const uint32_t v)
	{
		return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3);
	}

	// Everything calcTv() derives from an envelope's rate/shape byte
	// alone, tabulated per byte for both values of `nfs`.
	struct TvShape
	{
		uint8_t clock;		// which envelope-clock tap: 0-3 slow, 4 fast
		uint8_t exponential;	// the shaped (slew-limited) branch
		uint8_t shiftLinear;	// shift of the linear branch
		uint8_t shiftShaped;	// shift of the shaped branch
		uint16_t preshift;	// shaped branch's step before the sign flip
	};

	constexpr TvShape tvShape(const uint32_t speed, const bool nfs)
	{
		const bool w1 = (speed & 0xf0) == 0;
		const bool w2 = w1 || (speed & 0x10) != 0;
		const bool w3 = nfs && ((speed & 0x80) == 0 || ((speed & 0x40) == 0 && (!w2 || (speed & 0x20) == 0)));

		uint32_t type = (w2 ? 1 : 0) | (w3 ? 8 : 0);
		if(speed & 0x20)
			type |= 2;
		if((speed & 0x80) == 0 || (speed & 0x40) == 0)
			type |= 4;

		TvShape shape{};
		shape.clock = static_cast<uint8_t>((type & 4) ? 4 : (type & 3));
		shape.exponential = (type & 8) ? 1 : 0;
		shape.shiftLinear = static_cast<uint8_t>((10 - (speed & 15)) & 15);
		shape.shiftShaped = static_cast<uint8_t>((10 - (((speed >> 4) & 14) | (w2 ? 1 : 0))) & 15);
		shape.preshift = static_cast<uint16_t>(((speed & 15) << 9) | (w1 ? 0 : 0x2000));
		return shape;
	}

	struct TvShapeTable
	{
		TvShape entry[2][256];
		constexpr TvShapeTable() : entry{}
		{
			for(uint32_t nfs = 0; nfs < 2; ++nfs)
				for(uint32_t speed = 0; speed < 256; ++speed)
					entry[nfs][speed] = tvShape(speed, nfs != 0);
		}
	};
	constexpr TvShapeTable g_tvShapes;
} // namespace gpLib::gpInternal
