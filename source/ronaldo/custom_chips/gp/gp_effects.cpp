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

#include "gp_internal.h"

namespace gpLib
{
	using namespace gpInternal;

	int32_t GP::eramUnpack(const int32_t addr, const int32_t type) const
	{
		const uint32_t data = m_regs.eram[addr & 0x3fff];
		const int32_t sh = (data >> 14) & 3;
		// Sign-extend the 14-bit mantissa from bit 31, then shift down.
		const int32_t val = static_cast<int32_t>((data & 0x3fff) << 18);
		return sar(val, static_cast<uint32_t>(18 - sh * 2 + type));
	}

	void GP::eramPack(const int32_t addr, const int32_t val)
	{
		int32_t top = (val >> 13) & 0x7f;
		if(top & 0x40)
			top ^= 0x7f;
		int32_t sh;
		if(top >= 16)
			sh = 3;
		else if(top >= 4)
			sh = 2;
		else if(top >= 1)
			sh = 1;
		else
			sh = 0;

		uint32_t data = static_cast<uint32_t>(val >> (sh * 2)) & 0x3fff;
		data |= static_cast<uint32_t>(sh) << 14;
		m_regs.eram[addr & 0x3fff] = static_cast<uint16_t>(data);
	}

	void GP::effectsSection(EffectReturns& _returns)
	{
		auto* r1a = m_regs.ram1[FxA];
		auto* r1b = m_regs.ram1[FxB];
		auto* r2a = m_regs.ram2[FxA];
		auto* r2b = m_regs.ram2[FxB];
		auto* r2d = m_regs.ram2[Dac];
		auto* r2l = m_regs.ram2[Lfo];
		const int32_t tv = static_cast<int32_t>(m_regs.tv_counter);

		// Chorus crossfade gains from the LFO phase.
		{
			const int32_t phase = r2l[Phase];
			if(phase & 0x8000)
				r2l[9] = phase & 0x7fff;
			else
				r2l[10] = phase & 0x7fff;

			if((0x4000 - phase) & 0x8000)
				r2l[10] = (0x4000 - phase) & 0x7fff;
			else
				r2l[9] = (0x4000 - phase) & 0x7fff;
		}

		// One-pole input filters on the two send sums.
		{
			const int32_t v1 = r2l[1];
			const int32_t m1 = multi(r1b[1], static_cast<int8_t>(v1 >> 8)) >> 5;
			const int32_t m2 = multi(m_regs.rcsum[1], static_cast<int8_t>(v1 & 255)) >> 5;
			r1b[1] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);
		}
		{
			// The effects channel's own envelope, on the DAC channel's words.
			const int okey = (r2l[AddrControl] & 0x20) != 0;
			int unused = 0;
			calcTv(1, r2d[0], &r2d[9], okey, &unused);
		}
		{
			const int32_t v1 = r2d[1];
			const int32_t m1 = multi(r1b[0], static_cast<int8_t>(v1 >> 8)) >> 5;
			const int32_t m2 = multi(m_regs.rcsum[0], static_cast<int8_t>(v1 & 255)) >> 5;
			r1b[0] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);
		}

		// Steps 1-4: four all-pass diffuser stages. Each reads its delay
		// line, subtracts the feed-forward, and hands the result on.
		const auto allpass = [&](const uint32_t _in, const uint32_t _base, const int32_t _coef, uint32_t& _outState)
		{
			int32_t v2 = 0;
			const int32_t s1 = eramUnpack(_base + tv, 1);
			const int32_t s2 = eramUnpack(_base + tv);
			if((_coef & 0x30) != 0)
				v2 = s1;
			const uint32_t v3 = addclip20(_in, v2 ^ 0xfffff, 1);
			const int32_t m2 = multi(v3, static_cast<int8_t>(_coef & 255)) >> 5;
			_outState = addclip20(m2 >> 1, s2, m2 & 1);
			return v3;
		};
		{
			// 1
			const int32_t v1 = r2d[4];
			const int32_t m1 = multi(r1b[0], static_cast<int8_t>(v1 >> 8)) >> 6;
			r1b[4] = allpass(static_cast<uint32_t>(m1), r2a[1], v1, r1b[5]);
		}
		{
			// 2
			r1b[5] = allpass(r1b[5], r2a[2], r2d[4], r1a[0]);
		}
		{
			// 3
			r1a[0] = allpass(r1a[0], r2a[3], r2d[4], r1a[1]);
			r1a[2] = eramUnpack(r2a[5] + tv);
		}
		{
			// 4
			r1a[1] = allpass(r1a[1], r2a[4], r2d[5], r1a[3]);
			r1a[4] = eramUnpack(r2b[1] + tv);
		}
		{
			// 5: damping integrator A
			const int32_t v1 = r2d[7];
			const int32_t m1 = multi(r1b[2], static_cast<int8_t>(v1 >> 8)) >> 5;
			const int32_t s1 = eramUnpack(r2b[0] + tv);
			const int32_t m2 = multi(s1, static_cast<int8_t>(v1 & 255)) >> 5;
			r1b[2] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);

			eramPack(r2a[0] + tv, r1b[4]);
		}
		{
			// 6: damping integrator B
			const int32_t v1 = r2d[8];
			const int32_t m1 = multi(r1b[3], static_cast<int8_t>(v1 >> 8)) >> 5;
			const int32_t s1 = eramUnpack(r2b[8] + tv);
			const int32_t m2 = multi(s1, static_cast<int8_t>(v1 & 255)) >> 5;
			r1b[3] = addclip20(m1 >> 1, m2 >> 1, (m1 | m2) & 1);

			eramPack(r2a[1] + tv, r1b[5]);
			eramPack(r2a[2] + tv, r1a[0]);
		}
		{
			// 7
			const int32_t v1 = r2d[9];
			const uint32_t v2 = r1a[3];
			const int32_t m1 = multi(r1b[2], static_cast<int8_t>(v1 >> 8)) >> 5;
			const int32_t m2 = multi(r1b[3], static_cast<int8_t>(v1 >> 8)) >> 5;
			r1a[3] = addclip20(v2, m1 >> 1, m1 & 1);
			r1a[5] = addclip20(v2, m2 >> 1, m2 & 1);

			eramPack(r2a[3] + tv, r1a[1]);
		}
		// Steps 8-11: comb / all-pass pairs on coefficient r2d[6].
		const auto comb = [&](uint32_t& _acc, uint32_t& _state, const int32_t _coef)
		{
			const int32_t m1 = multi(_state, static_cast<int8_t>(_coef >> 8)) >> 5;
			const uint32_t v2 = addclip20(_acc, m1 >> 1, m1 & 1);
			_acc = v2;
			const int32_t m2 = multi(v2, static_cast<int8_t>(_coef & 255)) >> 5;
			_state = addclip20(_state, m2 >> 1, m2 & 1);
		};
		{
			// 8
			comb(r1a[3], r1a[2], r2d[6]);
			r1a[1] = eramUnpack(r2a[9] + tv);
		}
		{
			// 9
			comb(r1a[5], r1a[4], r2d[6]);
			r1b[4] = eramUnpack(r2b[5] + tv);
		}
		const auto combDelay = [&](uint32_t& _state, const uint32_t _base, const int32_t _coef, uint32_t& _out)
		{
			const uint32_t v2 = _state;
			const int32_t m1 = multi(v2, static_cast<int8_t>(_coef >> 8)) >> 5;
			const int32_t s1 = eramUnpack(_base + tv);
			const uint32_t v3 = addclip20(m1 >> 1, s1, m1 & 1);
			_state = v3;
			const int32_t m2 = multi(v3, static_cast<int8_t>(_coef & 255)) >> 5;
			_out = addclip20(m2 >> 1, v2, m2 & 1);
		};
		{
			// 10
			combDelay(r1a[1], r2a[8], r2d[6], r1b[5]);
			eramPack(r2a[4] + tv, r1a[3]);
		}
		{
			// 11
			combDelay(r1b[4], r2b[4], r2d[6], r1a[0]);
			eramPack(r2a[5] + tv, r1a[2]);
			eramPack(r2b[0] + tv, r1a[5]);
		}
		// Steps 12-16: output taps summed from the delay lines.
		{
			// 12
			r1a[5] = eramUnpack(r2a[6] + tv);
		}
		{
			// 13
			const int32_t s1 = eramUnpack(r2a[10] + tv);
			r1a[5] = addclip20(r1a[5], s1, 0);
			r1a[2] = eramUnpack(r2b[2] + tv);
		}
		{
			// 14
			const int32_t s1 = eramUnpack(r2b[6] + tv);
			const uint32_t t1 = addclip20(s1, r1a[2], 0);
			r1a[5] = addclip20(t1, r1a[5], 0);
			r1a[2] = eramUnpack(r2a[7] + tv);
		}
		{
			// 15
			const int32_t s1 = eramUnpack(r2a[11] + tv);
			r1a[2] = addclip20(r1a[2], s1, 0);
			r1a[3] = eramUnpack(r2b[3] + tv);
		}
		{
			// 16
			const int32_t s1 = eramUnpack(r2b[7] + tv);
			const uint32_t t1 = addclip20(s1, r1a[2], 0);
			r1a[2] = addclip20(t1, r1a[3], 0);

			eramPack(r2b[1] + tv, r1a[4]);
			eramPack(r2a[8] + tv, r1a[1]);
		}
		// Steps 17-18: reverb outputs, and the chorus taps read one ahead.
		const auto output = [&](const uint32_t _in, const int32_t _coef, const uint32_t _index)
		{
			_returns.mix[_index] = multi(_in, static_cast<int8_t>(_coef >> 8)) >> 5;
			_returns.send[_index] = multi(_in, static_cast<int8_t>(_coef & 255)) >> 5;
		};
		{
			// 17
			output(r1a[5], r2d[2], 0);
			const int32_t t1 = eramUnpack(r2b[10] + tv + 1);
			eramPack(r2a[9] + tv, r1b[5]);
			r1b[5] = t1;
		}
		{
			// 18
			output(r1a[2], r2d[3], 1);
			r1a[1] = eramUnpack(r2b[11] + tv + 1);
		}
		// Steps 19-20: chorus, crossfading each tap with its neighbour.
		const auto chorusTap = [&](const uint32_t _base, const int32_t _gain, const uint32_t _writeBase,
		                           const uint32_t _writeValue, uint32_t& _state)
		{
			const int32_t s1 = eramUnpack(_base + tv);
			eramPack(_writeBase + tv, _writeValue);
			const int32_t m1 = multi(s1, static_cast<int8_t>(_gain >> 8)) >> 5;
			const int32_t m2 = multi(_state, static_cast<int8_t>(_gain >> 8)) >> 5;
			const uint32_t t2 = addclip20(s1, (m1 >> 1) ^ 0xfffff, 1);
			_state = addclip20(t2, m2 >> 1, m2 & 1);
		};
		{
			// 19
			chorusTap(r2b[10], r2l[9], r2b[4], r1b[4], r1b[5]);
		}
		{
			// 20
			chorusTap(r2b[11], r2l[10], r2b[5], r1a[0], r1a[1]);
			eramPack(r2b[9] + tv, r1b[1]);
		}
		// Steps 21-23 and 31: chorus outputs.
		output(r1b[5], r2l[2], 2);
		output(r1b[5], r2l[3], 3);
		output(r1a[1], r2l[4], 4);
		output(r1a[1], r2l[5], 5);
	}

	void GP::chorusLfo()
	{
		auto* r1 = m_regs.ram1[Lfo];
		auto* r2 = m_regs.ram2[Lfo];

		const int key = 1;
		const int okey = (r2[AddrControl] & 0x20) != 0;
		const int active = key && okey;

		int b15 = (r2[Phase] & 0x8000) != 0;
		const int b6 = (r2[AddrControl] & 0x40) != 0;
		const int b7 = (r2[AddrControl] & 0x80) != 0;

		const uint32_t address = r1[AddrCurrent];
		const uint32_t address_end = r1[AddrEnd];
		const uint32_t address_loop = r1[AddrLoop];

		uint32_t sub_phase = r2[Phase] & 0x3fff;
		sub_phase += m_regs.ram2[r2[AddrControl] & 31][Pitch];
		const uint32_t sub_phase_of = (sub_phase >> 14) & 7;
		if(m_regs.nfs)
		{
			r2[Phase] &= ~0x3fff;
			r2[Phase] |= sub_phase & 0x3fff;
		}

		uint32_t address_cnt = address;
		int address_cmp = ((b15 ? address_loop : address_end) & 0xfffff) == (address_cnt & 0xfffff);
		int next_b15 = b15;
		uint32_t next_address = address_cnt;

		// One address step, exactly the voice's.
		{
			uint32_t address_cnt2 = (!b6 && address_cmp) ? address_loop : address_cnt;
			const int address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
			const int address_sub = !address_cmp && b6 && b15;
			if(b7)
				address_cnt2 -= address_add - address_sub;
			else
				address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0xfffff;
			b15 = b6 && (b15 ^ address_cmp);
		}

		if(sub_phase_of >= 1)
		{
			next_address = address_cnt;
			next_b15 = b15;
		}

		if(active && m_regs.nfs)
			r1[AddrCurrent] = next_address;

		if(m_regs.nfs)
		{
			r2[Phase] &= ~0x8000;
			r2[Phase] |= next_b15 << 15;
		}

		const uint32_t t2 = r1[AddrCurrent] - address_loop;
		m_regs.ram2[FxB][10] = static_cast<uint16_t>(address_end - t2);
		m_regs.ram2[FxB][11] = static_cast<uint16_t>(r1[AddrCurrent]);
	}
}
