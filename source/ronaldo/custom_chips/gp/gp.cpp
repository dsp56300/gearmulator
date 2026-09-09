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
#include "gp_internal.h"

#include <cstring>

namespace gpLib
{
	using namespace gpInternal;

	// ======================================================================
	// Host interface
	// ======================================================================

	GP::GP(const GpConfig _config) : m_config(_config)
	{
		reset();
	}

	void GP::reset()
	{
		memset(&m_regs, 0, sizeof(m_regs));
		m_dacPhase = 0;
		m_emitted = 0;
		m_dacOut[0] = m_dacOut[1] = {0, 0};
		driveIrq(false);
	}

	void GP::write8(uint32_t address, const uint8_t data)
	{
		address &= 0x3f;
		if(address < 0x4) // voice enable
		{
			const uint32_t shift = (3 - address) * 8;
			const uint32_t mask = address == 0 ? 0xf : 0xff;
			m_regs.voice_mask_pending &= ~(mask << shift);
			m_regs.voice_mask_pending |= (data & mask) << shift;
			m_regs.voice_mask_updating = 1;
		}
		else if(address >= 0x20 && address < 0x24) // wave rom read aperture
		{
			switch(address & 3)
			{
			case 1:
				m_regs.wave_read_address &= ~0xff0000;
				m_regs.wave_read_address |= (data & 0xff) << 16;
				break;
			case 2:
				m_regs.wave_read_address &= ~0xff00;
				m_regs.wave_read_address |= (data & 0xff) << 8;
				break;
			case 3:
				m_regs.wave_read_address &= ~0xff;
				m_regs.wave_read_address |= (data & 0xff) << 0;
				m_regs.wave_byte_latch = readRom(m_regs.wave_read_address);
				break;
			default:
				break;
			}
		}
		else if(address == 0x3c)
		{
			m_regs.config_reg_3c = data;
		}
		else if(address == 0x3d)
		{
			m_regs.config_reg_3d = data;
		}
		else if(address == 0x3e)
		{
			m_regs.select_channel = data & 0x1f;
		}
		else if((address >= 0x4 && address < 0x10) || (address >= 0x24 && address < 0x30))
		{
			// RAM1: three bytes assemble a 20-bit word, the third commits.
			switch(address & 3)
			{
			case 1:
				m_regs.write_latch &= ~0xf0000;
				m_regs.write_latch |= (data & 0xf) << 16;
				break;
			case 2:
				m_regs.write_latch &= ~0xff00;
				m_regs.write_latch |= (data & 0xff) << 8;
				break;
			case 3:
				m_regs.write_latch &= ~0xff;
				m_regs.write_latch |= (data & 0xff) << 0;
				m_regs.ram1[m_regs.select_channel][ram1Slot(address)] = m_regs.write_latch;
				break;
			default:
				break;
			}
		}
		else if((address >= 0x10 && address < 0x20) || (address >= 0x30 && address < 0x38))
		{
			// RAM2: two bytes, big-endian, the odd one commits.
			if((address & 1) == 0)
			{
				m_regs.write_latch &= ~0xff00;
				m_regs.write_latch |= (data & 0xff) << 8;
			}
			else
			{
				m_regs.write_latch &= ~0xff;
				m_regs.write_latch |= (data & 0xff) << 0;
				m_regs.ram2[m_regs.select_channel][ram2Slot(address)] = static_cast<uint16_t>(m_regs.write_latch);
			}
		}
	}

	uint8_t GP::read8(uint32_t address)
	{
		address &= 0x3f;

		if(address < 0x4)
		{
			// Any read of the mask window commits the pending mask.
			if(m_regs.voice_mask_updating)
				m_regs.voice_mask = m_regs.voice_mask_pending;
			m_regs.voice_mask_updating = 0;
		}
		else if(address == 0x3c || address == 0x3e) // status
		{
			uint8_t status = 0;
			if(address == 0x3e && m_regs.irq_assert)
			{
				m_regs.irq_assert = 0;
				driveIrq(false);
			}

			status |= static_cast<uint8_t>(m_regs.irq_channel);
			if(m_regs.voice_mask_updating)
				status |= 32;

			return status;
		}
		else if(address == 0x3f)
		{
			return m_regs.wave_byte_latch;
		}
		else if((address >= 0x4 && address < 0x10) || (address >= 0x24 && address < 0x30))
		{
			if((address & 3) == 1)
				m_regs.read_latch = m_regs.ram1[m_regs.select_channel][ram1Slot(address)];
		}
		else if((address >= 0x10 && address < 0x20) || (address >= 0x30 && address < 0x38))
		{
			if((address & 1) == 0)
				m_regs.read_latch = m_regs.ram2[m_regs.select_channel][ram2Slot(address)];
		}
		else if(address >= 0x39 && address <= 0x3b)
		{
			switch(address & 3)
			{
			case 1: return (m_regs.read_latch >> 16) & 0xf;
			case 2: return (m_regs.read_latch >> 8) & 0xff;
			case 3: return (m_regs.read_latch >> 0) & 0xff;
			default: break;
			}
		}

		return 0;
	}

	uint32_t GP::ram1Slot(const uint32_t _address)
	{
		// bit 5 -> slot bit 0, bits 3 and 2 INVERTED -> slot bits 2 and 1.
		uint32_t ix = 0;
		if(_address & 32) ix |= 1;
		if((_address & 8) == 0) ix |= 4;
		if((_address & 4) == 0) ix |= 2;
		return ix;
	}

	uint32_t GP::ram2Slot(const uint32_t _address)
	{
		uint32_t ix = (_address >> 1) & 7;
		if(_address & 32) ix |= 8;
		return ix;
	}

	void GP::setWaveRom(const uint8_t _bank, std::vector<uint8_t> _data)
	{
		if(_bank >= BankCount)
			return;
		m_waveRom[_bank] = std::move(_data);
	}

	bool GP::hasWaveRom(const uint8_t _bank) const
	{
		return _bank < BankCount && !m_waveRom[_bank].empty();
	}

	uint8_t GP::readRom(const uint32_t _address) const
	{
		// Config B bit 5 selects 512 KiB or 2 MiB banks; each bank wraps
		// within the chip the board fitted, unfitted banks read 0.
		const uint32_t bank = (_address >> ((m_regs.config_reg_3d & 0x20) ? 21 : 19)) & 7;
		const auto& rom = m_waveRom[bank];
		if(rom.empty())
			return 0;
		return rom[_address & (rom.size() - 1)];
	}

	void GP::driveIrq(const bool _level)
	{
		if(_level == m_irqLevel)
			return;
		m_irqLevel = _level;
		if(m_irqCallback)
			m_irqCallback(_level);
	}

	void GP::raiseVoiceEndIrq(const uint8_t _channel)
	{
		m_regs.irq_assert = 1;
		m_regs.irq_channel = _channel & 31;
		driveIrq(true);
	}

	void GP::emitSample(const int32_t _left, const int32_t _right)
	{
		// `_left`/`_right` are the 20-bit DAC words at bit 12; keep the full
		// precision and scale to OutputFullScale.
		if(m_emitted < 2)
			m_dacOut[m_emitted] = { sar(_left, 9), sar(_right, 9) };
		++m_emitted;
	}

	GP::SampleFrame GP::renderFrame()
	{
		if(m_dacPhase == 0)
			runIteration();

		const SampleFrame out = m_dacOut[m_dacPhase];
		// With oversampling the iteration posted two sub-samples and the
		// next call clocks out the second; without it every call is a fresh
		// iteration.
		m_dacPhase = (m_dacPhase == 0 && m_emitted > 1) ? 1 : 0;
		return out;
	}

	// ======================================================================
	// The iteration
	// ======================================================================

	void GP::runIteration()
	{
		m_emitted = 0;

		dacStage();

		// Global envelope/ERAM counter: a 14-bit down-counter, loaded from the
		// chorus LFO phase on the very first iteration.
		if(!m_regs.nfs)
			m_regs.tv_counter = m_regs.ram2[Lfo][Phase];
		m_regs.tv_counter = (m_regs.tv_counter - 1) & 0x3fff;
		prepareEnvelopeClock();

		// Wave-ROM bank decode for this iteration.
		m_bankShift = (m_regs.config_reg_3d & 0x20) ? 21 : 19;
		for(uint32_t bank = 0; bank < BankCount; ++bank)
		{
			const auto& rom = m_waveRom[bank];
			m_bankData[bank] = rom.empty() ? nullptr : rom.data();
			m_bankMask[bank] = rom.empty() ? 0 : static_cast<uint32_t>(rom.size() - 1);
		}

		EffectReturns returns{};
		effectsSection(returns);
		chorusLfo();
		voicePass(returns);

		if(m_regs.nfs)
			m_regs.ram2[Lfo][AddrControl] |= 0x20;
		m_regs.nfs = 1;
	}

	// The envelope clock: calcTv() picks four bits of tv_counter, reversed,
	// as a small additive term and gates its write-back on the low bits being
	// zero. Which bits depends only on the ramp type, so precompute the five
	// variants here rather than per envelope per voice.
	void GP::prepareEnvelopeClock()
	{
		const uint32_t tv = m_regs.tv_counter;
		// Fast ramps (type & 4): bits 3..0, written every iteration.
		m_tvAddLow[4] = bitReverse4(tv & 15);
		m_tvWrite[4] = 1;
		// Slow ramps: bits (5+2t)..(2+2t), written when the bits below are zero.
		static constexpr uint32_t writeMask[4] = {3, 15, 63, 127};
		for(uint32_t t = 0; t < 4; ++t)
		{
			m_tvAddLow[t] = bitReverse4((tv >> (2 + 2 * t)) & 15);
			m_tvWrite[t] = (tv & writeMask[t]) == 0 ? 1 : 0;
		}
	}

	// ---- DAC stage -----------------------------------------------------------
	//
	// Adds the noise-shaper error of the previous sample back into the mix
	// accumulator, dithers, and posts one sample — or two with 2x oversampling,
	// the second one from the same accumulator after another shaper round.
	void GP::dacStage()
	{
		const uint32_t cfg = m_regs.config_reg_3c;
		uint32_t noiseMask = 0;
		uint32_t orval = 0;
		uint32_t writeMask;
		if((cfg & 0x30) != 0)
		{
			switch((cfg >> 2) & 3)
			{
			case 1: noiseMask = 3; break;
			case 2: noiseMask = 7; break;
			case 3: noiseMask = 15; break;
			default: break;
			}
			switch(cfg & 3)
			{
			case 1: orval |= 1 << 8; break;
			case 2: orval |= 1 << 10; break;
			default: break;
			}
			writeMask = 15;
		}
		else
		{
			switch((cfg >> 2) & 3)
			{
			case 2: noiseMask = 1; break;
			case 3: noiseMask = 3; break;
			default: break;
			}
			switch(cfg & 3)
			{
			case 1: orval |= 1 << 6; break;
			case 2: orval |= 1 << 8; break;
			default: break;
			}
			writeMask = 3;
		}
		if((cfg & 0x80) == 0)	// noise shaping off: nothing carried
			writeMask = 0;
		if((cfg & 0x30) == 0x30)
			orval |= 1 << 12;

		auto* dac1 = m_regs.ram1[Dac];
		const auto lfsrStep = [](const uint32_t _s)
		{
			const uint32_t xr = ((_s >> 0) ^ (_s >> 1) ^ (_s >> 7) ^ (_s >> 12)) & 1;
			return (_s >> 1) | (xr << 15);
		};

		// ---- sub-sample 0 ----
		uint32_t shifter = lfsrStep(m_regs.ram2[Dac][10]);
		m_regs.ram2[Dac][10] = static_cast<uint16_t>(shifter);

		m_regs.accum_l = static_cast<int>(addclip20(m_regs.accum_l, dac1[0], 0));
		m_regs.accum_r = static_cast<int>(addclip20(m_regs.accum_r, dac1[1], 0));
		dac1[2] = addclip20(m_regs.accum_l, orval | (shifter & noiseMask), 0);
		dac1[4] = addclip20(m_regs.accum_r, orval | (shifter & noiseMask), 0);
		dac1[0] = m_regs.accum_l & writeMask;
		dac1[1] = m_regs.accum_r & writeMask;
		emitSample(static_cast<int32_t>((dac1[2] & ~writeMask) << 12),
		           static_cast<int32_t>((dac1[4] & ~writeMask) << 12));

		// ---- sub-sample 1 ----
		// The shaper always runs a second round; it is only committed and
		// emitted with oversampling on.
		shifter = lfsrStep(shifter);
		m_regs.accum_l = static_cast<int>(addclip20(m_regs.accum_l, dac1[0], 0));
		m_regs.accum_r = static_cast<int>(addclip20(m_regs.accum_r, dac1[1], 0));
		dac1[3] = addclip20(m_regs.accum_l, orval | (shifter & noiseMask), 0);
		dac1[5] = addclip20(m_regs.accum_r, orval | (shifter & noiseMask), 0);

		if(cfg & 0x40)
		{
			m_regs.ram2[Dac][10] = static_cast<uint16_t>(shifter);
			dac1[0] = m_regs.accum_l & writeMask;
			dac1[1] = m_regs.accum_r & writeMask;
			emitSample(static_cast<int32_t>((dac1[3] & ~writeMask) << 12),
			           static_cast<int32_t>((dac1[5] & ~writeMask) << 12));
		}
	}

	// ---- envelopes -------------------------------------------------------------
	//
	// One time-variant ramp: `adjust` is the control word ([7:0] rate and
	// shape, [15:8] target), `levelcur` the 15-bit level it drives. `e` is
	// which of the three ramps this is; the cutoff ramp (2) only tracks the
	// target while the voice is active. `volmul` receives the volume
	// multiplier for the two volume ramps.
	void GP::calcTv(const int e, const int adjust, uint16_t* levelcur, const int active, int* volmul) const
	{
		*levelcur &= 0x7fff;
		const int speed = adjust & 0xff;
		const int target = (adjust >> 8) & 0xff;
		const TvShape& shape = g_tvShapes.entry[m_regs.nfs ? 1 : 0][speed];

		const int addlow = static_cast<int>(m_tvAddLow[shape.clock]);
		const int write = !active || m_tvWrite[shape.clock];

		int sum1 = target << 11;
		if(e != 2 || active)
			sum1 -= *levelcur << 4;

		if(!shape.exponential)
		{
			const int shifted = sar(sum1, shape.shiftLinear) - sum1;
			const int sum2 = (target << 11) + addlow + shifted;
			if(write && m_regs.nfs)
				*levelcur = static_cast<uint16_t>((sum2 >> 4) & 0x7fff);

			if(e != 2)
				*volmul = (sum2 >> 4) & 0x7ffe;
		}
		else
		{
			const int neg = (sum1 & 0x80000) != 0;
			int preshift = shape.preshift;
			if(neg)
				preshift ^= ~0x3f;

			int sum2 = sar(preshift, shape.shiftShaped);
			if(e != 2 || active)
				sum2 += (*levelcur << 4) | addlow;

			const int sum2_l = sar(sum2, 4);
			const int sum3 = (target << 11) - (sum2_l << 4);

			const int neg2 = (sum3 & 0x80000) != 0;
			const int xnor = !(neg2 ^ neg);

			if(write && m_regs.nfs)
			{
				if(xnor)
					*levelcur = static_cast<uint16_t>(sum2_l & 0x7fff);
				else
					*levelcur = static_cast<uint16_t>(target << 7);
			}

			if(e == 0)
				*volmul = sum2_l & 0x7ffe;
			else if(e == 1)
				*volmul = xnor ? (sum2_l & 0x7ffe) : (target << 7);
		}
	}

	// ---- voice pass ---------------------------------------------------------------

	void GP::voicePass(const EffectReturns& _returns)
	{
		const uint32_t regSlots = voiceSlots();
		// A voice plays only where BOTH mask copies agree.
		const uint32_t voiceActive = m_regs.voice_mask & m_regs.voice_mask_pending;

		auto* mix = m_regs.ram1[Lfo];
		mix[MixL] = 0;
		mix[MixR] = 0;
		m_regs.rcsum[0] = 0;
		m_regs.rcsum[1] = 0;

		for(uint32_t slot = 0; slot < regSlots; ++slot)
		{
			const int key = (voiceActive >> slot) & 1;
			// The key latch as it was BEFORE this iteration: renderVoice()
			// sets it on a key-on, and everything below must still see the
			// key-on iteration as inactive.
			const int okey = (m_regs.ram2[slot][AddrControl] & 0x20) != 0;
			const int active = okey && key;

			// The chip runs every voice in full and then zeroes the result of
			// the silent ones; with the key off nothing survives except the
			// cutoff envelope's level, so do just that. Two exceptions keep
			// the full path: the very first iteration, where the write-back
			// gate is off and the zeroing does not happen; and a slot count
			// beyond the 28 voice channels, where the pass runs over the
			// effects channels and the filter write-backs land on the mix
			// accumulators and effect state.
			const bool silent = !key && m_regs.nfs && slot < DspChannelFirst;
			const uint32_t sample3 = silent ? silentVoice(slot) : renderVoice(slot, key);

			// The effect returns are folded into the mix at fixed time slots.
			const uint32_t slot2 = (slot == regSlots - 1) ? 31 : slot + 1;
			int returnIndex = -1;
			switch(slot2)
			{
			case 17: returnIndex = 0; break;
			case 18: returnIndex = 1; break;
			case 21: returnIndex = 2; break;
			case 22: returnIndex = 3; break;
			case 23: returnIndex = 4; break;
			case 31: returnIndex = 5; break;
			default: break;
			}
			if(returnIndex >= 0)
			{
				const int32_t m = _returns.mix[returnIndex];
				const int32_t s = _returns.send[returnIndex];
				// Even-numbered returns go left / to send 0 (the 17/18 pair
				// both feed send 1), odd ones right / to send 1.
				static constexpr uint32_t mixSlot[6] = {MixL, MixR, MixL, MixR, MixL, MixR};
				static constexpr uint32_t sendSlot[6] = {1, 1, 0, 1, 0, 1};
				mix[mixSlot[returnIndex]] = addclip20(mix[mixSlot[returnIndex]], m >> 1, m & 1);
				m_regs.rcsum[sendSlot[returnIndex]] =
					static_cast<int>(addclip20(m_regs.rcsum[sendSlot[returnIndex]], s >> 1, s & 1));
			}

			// Pan and sends: an inactive voice's coefficients read as zero.
			const uint32_t pan = active ? m_regs.ram2[slot][Pan] : 0;
			const uint32_t rc = active ? m_regs.ram2[slot][Sends] : 0;

			const int32_t sampl = multi(sample3, static_cast<int8_t>((pan >> 8) & 255));
			const int32_t sampr = multi(sample3, static_cast<int8_t>((pan >> 0) & 255));
			const int32_t rc0 = multi(sample3, static_cast<int8_t>((rc >> 8) & 255)) >> 5;
			const int32_t rc1 = multi(sample3, static_cast<int8_t>((rc >> 0) & 255)) >> 5;

			const uint32_t suml = addclip20(mix[MixL], sampl >> 6, (sampl >> 5) & 1);
			const uint32_t sumr = addclip20(mix[MixR], sampr >> 6, (sampr >> 5) & 1);

			m_regs.rcsum[0] = static_cast<int>(addclip20(m_regs.rcsum[0], rc0 >> 1, rc0 & 1));
			m_regs.rcsum[1] = static_cast<int>(addclip20(m_regs.rcsum[1], rc1 >> 1, rc1 & 1));

			if(slot != regSlots - 1)
			{
				mix[MixL] = suml;
				mix[MixR] = sumr;
			}
			else
			{
				m_regs.accum_l = static_cast<int>(suml);
				m_regs.accum_r = static_cast<int>(sumr);
			}

			if(!active)
			{
				auto* ram1 = m_regs.ram1[slot];
				auto* ram2 = m_regs.ram2[slot];
				if(m_regs.nfs)
				{
					ram1[TvfBandpass] = 0;
					ram1[TvfLowpass] = 0;
					ram1[DpcmAccum] = 0;
				}
				ram2[Phase] = 0;
				ram2[TvVolume1Level] = 0;
				ram2[TvVolume2Level] = 0;
			}
		}
	}

	// A voice with its key off: only its cutoff envelope keeps tracking.
	uint32_t GP::silentVoice(const uint32_t _slot)
	{
		auto* ram2 = m_regs.ram2[_slot];
		calcTv(2, ram2[TvCutoff], &ram2[TvCutoffLevel], 0, nullptr);
		return 0;
	}

	// The full voice datapath: address generator and DPCM decode over four
	// ROM taps, three-tap interpolation, the state-variable filter, the three
	// envelopes and the two volume stages. Returns the voice's output sample
	// before pan and sends.
	uint32_t GP::renderVoice(const uint32_t _slot, const int key)
	{
		auto* ram1 = m_regs.ram1[_slot];
		auto* ram2 = m_regs.ram2[_slot];

		const int okey = (ram2[AddrControl] & 0x20) != 0;
		const int active = okey && key;
		const int kon = key && !okey;

		// ---- address generator ----------------------------------------------
		int b15 = (ram2[Phase] & 0x8000) != 0;
		const int b6 = (ram2[AddrControl] & 0x40) != 0;
		const int b7 = (ram2[AddrControl] & 0x80) != 0;
		const uint32_t hiaddr = (ram2[AddrControl] >> 8) & 15;
		const uint32_t old_nibble = (ram2[AddrControl] >> 12) & 15;

		const uint32_t address = ram1[AddrCurrent];
		const uint32_t address_end = ram1[AddrEnd];
		const uint32_t address_loop = ram1[AddrLoop];

		const uint32_t cmp1 = b15 ? address_loop : address_end;
		const int nibble_cmp1 = (cmp1 & 0xffff0) == (address & 0xffff0);

		int irq_flag;
		if(kon)
			irq_flag = ((cmp1 + address_loop) & 0x100000) != 0;
		else
			irq_flag = ((address + ((-address_loop) & 0xfffff)) & 0x100000) != 0;
		irq_flag ^= b7;

		// The exponent nibble for the block this sample lands in.
		const uint32_t nibble_address = (!b6 && nibble_cmp1) ? address_loop : address;
		const int address_b4 = (nibble_address & 0x10) != 0;
		uint32_t wave_address = nibble_address >> 5;
		const int xor2 = address_b4 ^ b7;
		const int check1 = xor2 && active;
		const int xor1 = b15 ^ !nibble_cmp1;
		const int nibble_add = b6 ? (check1 && xor1) : (!nibble_cmp1 && check1);
		const int nibble_subtract = b6 && !xor1 && active && !xor2;
		if(b7)
			wave_address -= nibble_add - nibble_subtract;
		else
			wave_address += nibble_add - nibble_subtract;
		wave_address &= 0xfffff;

		uint32_t newnibble = fetchRom((hiaddr << 20) | wave_address);
		const int newnibble_sel = address_b4 ^ ((b6 || !nibble_cmp1) && okey);
		newnibble = newnibble_sel ? (newnibble >> 4) & 15 : newnibble & 15;

		uint32_t sub_phase = ram2[Phase] & 0x3fff;
		const uint32_t interp_ratio = (sub_phase >> 7) & 127;
		sub_phase += m_regs.ram2[ram2[AddrControl] & 31][Pitch];
		const uint32_t sub_phase_of = (sub_phase >> 14) & 7;
		if(m_regs.nfs)
		{
			ram2[Phase] &= ~0x3fff;
			ram2[Phase] |= sub_phase & 0x3fff;
		}

		// Walk the play pointer through up to four source steps, fetching a
		// sample byte at each of the first four positions. The pointer the
		// voice keeps is the one after `sub_phase_of` steps.
		int8_t samp[4];
		int nibble_cmp[5];	// per fetched position: still in the block the nibble was cached for?
		uint32_t address_cnt = address;
		int address_cmp = ((b15 ? address_loop : address_end) & 0xfffff) == (address_cnt & 0xfffff);
		uint32_t next_address = address_cnt;
		int next_b15 = b15;
		int usenew = 0;

		samp[0] = static_cast<int8_t>(fetchRom((hiaddr << 20) | address_cnt));
		nibble_cmp[0] = 1;	// the first tap is at `address` itself

		for(uint32_t step = 1; step <= 4; ++step)
		{
			uint32_t address_cnt2 = (!b6 && address_cmp) ? address_loop : address_cnt;
			const int address_add = (!address_cmp && b6 && !b15) || (!address_cmp && !b6);
			const int address_sub = !address_cmp && b6 && b15;
			if(b7)
				address_cnt2 -= address_add - address_sub;
			else
				address_cnt2 += address_add - address_sub;
			address_cnt = address_cnt2 & 0xfffff;
			// The direction latch is not advanced by the fourth step.
			if(step < 4)
				b15 = b6 && (b15 ^ address_cmp);

			if(step < 4)
				samp[step] = static_cast<int8_t>(fetchRom((hiaddr << 20) | address_cnt));
			nibble_cmp[step] = (address & 0xffff0) == (address_cnt & 0xffff0);
			if(step < 4)
				address_cmp = ((b15 ? address_loop : address_end) & 0xfffff) == (address_cnt & 0xfffff);

			if(sub_phase_of >= step)
			{
				next_address = address_cnt;
				usenew = !nibble_cmp[step];
				if(step < 4)
					next_b15 = b15;
			}
		}

		if(active && m_regs.nfs)
			ram1[AddrCurrent] = next_address;

		if(m_regs.nfs)
		{
			ram2[Phase] &= ~0x8000;
			ram2[Phase] |= next_b15 << 15;
		}

		// ---- DPCM: accumulate the consumed deltas ---------------------------
		// Each delta is scaled by the block's exponent nibble: the cached one
		// while the tap is still in the cached block, the freshly fetched one
		// otherwise.
		const auto scaled = [&](const uint32_t tap)
		{
			const uint32_t select_nibble = nibble_cmp[tap] ? old_nibble : newnibble;
			const uint32_t shift = (10 - select_nibble) & 15;
			return sar(samp[tap] * 2048, shift);
		};

		uint32_t accum = ram1[DpcmAccum];
		for(uint32_t tap = 0; tap < 4; ++tap)
		{
			const int32_t shifted = scaled(tap);
			if(sub_phase_of >= tap + 1)
				accum = addclip20(accum, shifted >> 1, shifted & 1);
		}

		// ---- interpolation: three taps weighted by the sub-phase ------------
		uint32_t interpolated = ram1[DpcmAccum];
		for(uint32_t tap = 0; tap < 3; ++tap)
		{
			int32_t step = multi(static_cast<uint32_t>(interp_lut[tap][interp_ratio] << 6), samp[tap]) >> 8;
			const uint32_t select_nibble = nibble_cmp[tap] ? old_nibble : newnibble;
			const uint32_t shift = (10 - select_nibble) & 15;
			step = sar(step * 2, shift);
			interpolated = addclip20(interpolated, step >> 1, step & 1);
		}

		// ---- state-variable filter ----------------------------------------------
		const int32_t reg1 = static_cast<int32_t>(ram1[TvfBandpass]);
		const int32_t reg3 = static_cast<int32_t>(ram1[TvfLowpass]);
		const int32_t reg2_6 = (ram2[Control] >> 8) & 127;
		const int32_t filter = ram2[TvCutoffLevel];
		const int8_t coarse = static_cast<int8_t>(filter >> 8);
		const int8_t fine = static_cast<int8_t>((filter >> 1) & 127);
		int32_t v3;

		if(m_config.saturatingFilter)
		{
			// First-generation silicon keeps the filter inside the 20-bit
			// saturating datapath.
			const int32_t mult1 = multi(reg1, coarse);
			const int32_t mult2 = multi(reg1, fine);
			const int32_t mult3 = multi(reg1, static_cast<int8_t>(reg2_6));

			const uint32_t v2 = addclip20(reg3, mult1 >> 6, (mult1 >> 5) & 1);
			const uint32_t v1 = addclip20(v2, mult2 >> 13, (mult2 >> 12) & 1);
			const uint32_t subvar = addclip20(v1, mult3 >> 6, (mult3 >> 5) & 1);

			ram1[TvfLowpass] = v1;

			v3 = static_cast<int32_t>(addclip20(interpolated, subvar ^ 0xfffff, 1));

			const int32_t mult4 = multi(v3, coarse);
			const int32_t mult5 = multi(v3, fine);
			const uint32_t v4 = addclip20(reg1, mult4 >> 6, (mult4 >> 5) & 1);
			const uint32_t v5 = addclip20(v4, mult5 >> 13, (mult5 >> 12) & 1);

			ram1[TvfBandpass] = v5;
		}
		else
		{
			// Later revisions carry the intermediates at full width; the
			// integrators hold more than 20 bits and are stored unmasked.
			const int32_t mult1 = reg1 * coarse;
			const int32_t mult2 = reg1 * fine;
			const int32_t mult3 = reg1 * static_cast<int8_t>(reg2_6);

			const int32_t v2 = reg3 + (mult1 >> 6) + ((mult1 >> 5) & 1);
			const int32_t v1 = v2 + (mult2 >> 13) + ((mult2 >> 12) & 1);
			const int32_t subvar = v1 + (mult3 >> 6) + ((mult3 >> 5) & 1);

			ram1[TvfLowpass] = static_cast<uint32_t>(v1);

			v3 = sign20(interpolated) - subvar;

			const int32_t mult4 = v3 * coarse;
			const int32_t mult5 = v3 * fine;
			const int32_t v4 = reg1 + (mult4 >> 6) + ((mult4 >> 5) & 1);
			const int32_t v5 = v4 + (mult5 >> 13) + ((mult5 >> 12) & 1);

			ram1[TvfBandpass] = static_cast<uint32_t>(v5);
		}

		ram1[DpcmAccum] = accum;

		// ---- voice-end interrupt -------------------------------------------
		if(active && (ram2[Control] & 1) != 0 && (ram2[Phase] & 0x4000) == 0 && !m_regs.irq_assert && irq_flag)
		{
			if(m_regs.nfs)
				ram2[Phase] |= 0x4000;
			m_regs.irq_assert = 1;
			m_regs.irq_channel = _slot;
			driveIrq(true);
		}

		// ---- envelopes and volume ------------------------------------------------
		int volmul1 = 0;
		int volmul2 = 0;
		calcTv(0, ram2[TvVolume1], &ram2[TvVolume1Level], active, &volmul1);
		calcTv(1, ram2[TvVolume2], &ram2[TvVolume2Level], active, &volmul2);
		calcTv(2, ram2[TvCutoff], &ram2[TvCutoffLevel], active, nullptr);

		const uint32_t sample = (ram2[Control] & 2) == 0 ? ram1[TvfLowpass] : static_cast<uint32_t>(v3);

		const int32_t multiv1 = multi(sample, static_cast<int8_t>(volmul1 >> 8));
		const int32_t multiv2 = multi(sample, static_cast<int8_t>((volmul1 >> 1) & 127));
		const uint32_t sample2 = addclip20(multiv1 >> 6, multiv2 >> 13, ((multiv2 >> 12) | (multiv1 >> 5)) & 1);

		const int32_t multiv3 = multi(sample2, static_cast<int8_t>(volmul2 >> 8));
		const int32_t multiv4 = multi(sample2, static_cast<int8_t>((volmul2 >> 1) & 127));
		const uint32_t sample3 = addclip20(multiv3 >> 6, multiv4 >> 13, ((multiv4 >> 12) | (multiv3 >> 5)) & 1);

		// ---- key latch and cached nibble -------------------------------------
		if(key && m_regs.nfs)
		{
			ram2[AddrControl] &= ~0xf020;
			ram2[AddrControl] |= static_cast<uint16_t>(((usenew || kon) ? newnibble : old_nibble) << 12);
			ram2[AddrControl] |= static_cast<uint16_t>(key << 5);
		}

		return sample3;
	}
}
