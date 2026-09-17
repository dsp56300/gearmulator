// Thanks to ValleyBell for the original research on the LP chip.
// Chorus and tremolo voices are not modelled yet.
#include "lp.h"
#include "../pcmInterpolation.h"

#include <algorithm>

namespace lpLib
{
	namespace
	{
		// The MB87420 contains one 384 x 12-bit mask ROM: three interpolation
		// coefficients for each of the 128 fractional phases. It decodes to the
		// same table the GP and XP use, so the three chips share one copy.
		constexpr auto& InterpolationRom = pcmInterpolation::interpolationCoefficients;

		uint16_t read16(const std::array<uint8_t, 0x100>& _data, const size_t _offset)
		{
			return uint16_t(_data[_offset] | (_data[_offset + 1] << 8));
		}

		int32_t wrap12(const int32_t _value)
		{
			const uint32_t value = static_cast<uint32_t>(_value) & 0xfff;
			return (value & 0x800) != 0 ? static_cast<int32_t>(value) - 0x1000 : static_cast<int32_t>(value);
		}

	}

	LP::LP(const std::vector<uint8_t>& _rom) : m_rom(_rom)
	{
		reset();
	}

	uint16_t LP::parameterWord(const unsigned _voice, const unsigned _word) const
	{
		if (_voice >= VoiceCount || _word >= 8) return 0;
		return _word < 4 ? m_lp1RamA[_voice * 4 + _word] : m_lp1RamB[_voice * 4 + (_word - 4)];
	}

	void LP::setParameterWord(const unsigned _voice, const unsigned _word, const uint16_t _value)
	{
		if (_voice >= VoiceCount || _word >= 8) return;
		if (_word < 4) m_lp1RamA[_voice * 4 + _word] = _value;
		else m_lp1RamB[_voice * 4 + (_word - 4)] = _value;
	}

	int32_t LP::currentVolume(const unsigned _voice) const
	{
		return int32_t(parameterWord(_voice, 0)) | (int32_t(parameterWord(_voice, 1) & 0x03ff) << 16);
	}

	void LP::setCurrentVolume(const unsigned _voice, const int32_t _value)
	{
		setParameterWord(_voice, 0, static_cast<uint16_t>(_value));
		setParameterWord(_voice, 1, static_cast<uint16_t>((parameterWord(_voice, 1) & 0xfc00)
			| ((static_cast<uint32_t>(_value) >> 16) & 0x03ff)));
	}

	uint32_t LP::currentAddress(const unsigned _voice) const
	{
		return uint32_t(parameterWord(_voice, 4)) | (uint32_t(parameterWord(_voice, 5)) << 16);
	}

	void LP::setCurrentAddress(const unsigned _voice, const uint32_t _value)
	{
		setParameterWord(_voice, 4, static_cast<uint16_t>(_value));
		setParameterWord(_voice, 5, static_cast<uint16_t>(_value >> 16));
	}

	int32_t LP::predictor(const unsigned _voice) const
	{
		if (_voice >= VoiceCount) return 0;
		const uint16_t value = m_lp2Ram[_voice] & 0x0fff;
		return (value & 0x0800) != 0 ? int32_t(value) - 0x1000 : int32_t(value);
	}

	void LP::setPredictor(const unsigned _voice, const int32_t _value)
	{
		if (_voice < VoiceCount) m_lp2Ram[_voice] = static_cast<uint16_t>(_value) & 0x0fff;
	}

	void LP::reset()
	{
		m_lp1RamA.fill(0);
		m_lp1RamB.fill(0);
		m_lp2Ram.fill(0);
		m_voiceRuntime = {};
		m_registers.fill(0);
		m_irqQueue.fill(0);
		m_selected = m_irqRead = m_irqWrite = m_irqCount = m_irqVoice = 0;
		m_irqCurrentValid = false;
		m_readback = 0;
		setInterruptLine(false);
	}

	uint8_t LP::read(const uint8_t _offset)
	{
		switch (_offset & 3)
		{
		case 0:
			{
				const uint8_t result = m_irqVoice;
				if (m_irqCurrentValid)
				{
					if (m_irqCount)
					{
						m_irqVoice = m_irqQueue[m_irqRead];
						m_irqRead = static_cast<uint8_t>((m_irqRead + 1) % VoiceCount);
						--m_irqCount;
						setInterruptLine(false);
						setInterruptLine(true);
					}
					else
					{
						m_irqCurrentValid = false;
						setInterruptLine(false);
					}
				}
				return result;
			}
		case 1:
			{
				const uint32_t bank = m_registers[3] & 0x3c;
				uint32_t address = uint32_t(m_registers[9]) | (uint32_t(m_registers[10]) << 8)
					| (uint32_t(m_registers[11]) << 16);
				address = (((address >> 6) + 2) & 0x3ffff) | (bank << 16);
				return readRom(address);
			}
		case 2: return static_cast<uint8_t>(m_readback);
		case 3: return static_cast<uint8_t>(m_readback >> 8);
		default: return 0xff;
		}
	}

	void LP::write(const uint8_t _offset, const uint8_t _data)
	{
		m_registers[_offset] = _data;
		if (_offset < 0x10)
		{
			VoiceRuntime& runtime = m_voiceRuntime[m_selected];
			switch (_offset)
			{
			case 0x00: setParameterWord(m_selected, 0, read16(m_registers, 0)); break;
			case 0x02: setParameterWord(m_selected, 1, read16(m_registers, 2)); break;
			case 0x04: setParameterWord(m_selected, 2, read16(m_registers, 4)); break;
			case 0x06:
				{
					const uint16_t value = read16(m_registers, 6);
					const bool changed = parameterWord(m_selected, 3) != value;
					setParameterWord(m_selected, 3, value);
					if (runtime.enabled && changed) runtime.envelopeActive = true;
				}
				break;
			case 0x08: setParameterWord(m_selected, 4, read16(m_registers, 8)); break;
			case 0x0a: setParameterWord(m_selected, 5, read16(m_registers, 10)); break;
			case 0x0c: setParameterWord(m_selected, 6, read16(m_registers, 12)); break;
			case 0x0e: setParameterWord(m_selected, 7, read16(m_registers, 14)); break;
			default: break;
			}
			return;
		}

		switch (_offset)
		{
		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
			{
				const uint8_t first = static_cast<uint8_t>(((_offset >> 1) & 3) * 8);
				for (uint8_t i = 0; i < 8; ++i)
				{
					VoiceRuntime& runtime = m_voiceRuntime[first + i];
					const bool enabled = (_data & (1 << i)) != 0;
					if (enabled && !runtime.enabled)
					{
						runtime.playDirection = 0;
						runtime.envelopeActive = true;
						setPredictor(first + i, 0);
					}
					runtime.enabled = enabled;
				}
			}
			break;

		case 0x10: m_readback = parameterWord(_data & 0x1f, 0); break;
		case 0x12: m_readback = parameterWord(_data & 0x1f, 1); break;
		case 0x14: m_readback = parameterWord(_data & 0x1f, 2); break;
		case 0x16: m_readback = parameterWord(_data & 0x1f, 3); break;
		case 0x18: m_readback = parameterWord(_data & 0x1f, 4); break;
		case 0x1a: m_readback = parameterWord(_data & 0x1f, 5); break;
		case 0x1c: m_readback = parameterWord(_data & 0x1f, 6); break;
		case 0x1e: m_readback = parameterWord(_data & 0x1f, 7); break;
		case 0x1f: m_selected = _data & 0x1f; break;
		default: break;
		}
	}

	LP::Voices LP::renderSample()
	{
		Voices result{};
		for (size_t i = 0; i < VoiceCount; ++i)
		{
			VoiceRuntime& runtime = m_voiceRuntime[i];
			if (!runtime.enabled) continue;

			int32_t volume = currentVolume(static_cast<unsigned>(i));
			const uint16_t bankVolume = parameterWord(static_cast<unsigned>(i), 1);
			const uint8_t bankMode = static_cast<uint8_t>(bankVolume >> 8) & 0xfc;
			const uint16_t step = parameterWord(static_cast<unsigned>(i), 2);
			const uint16_t envelope = parameterWord(static_cast<unsigned>(i), 3);
			const uint8_t envelopeRate = static_cast<uint8_t>(envelope);
			const uint8_t envelopeTarget = static_cast<uint8_t>(envelope >> 8);
			const uint32_t voiceAddress = currentAddress(static_cast<unsigned>(i));
			const uint16_t end = parameterWord(static_cast<unsigned>(i), 6);
			const uint16_t loop = parameterWord(static_cast<unsigned>(i), 7);

			const uint32_t highPhase = (voiceAddress >> 14) & 0x3ffff;
			const uint32_t oldSubPhase = voiceAddress & 0x3fff;
			const uint8_t interpolationPhase = static_cast<uint8_t>((oldSubPhase >> 7) & 0x7f);
			const bool alternateLoop = (bankMode & 0x80) != 0;
			const uint32_t subPhase = oldSubPhase + step;
			const unsigned crossedBytes = subPhase >> 14;
			const uint32_t romBank = ((bankMode & 0x3c) >> 2) << 18;
			const int firstStep = (bankMode & 0x40) != 0 ? -1 : 1;
			const uint32_t loopAddress = uint32_t(loop) << 2;
			const uint32_t endAddress = uint32_t(end) << 2;
			// The byte at loopAddress is the DPCM anchor.  Repeating playback
			// starts with the following byte, for both forward and alternate
			// loops.  In alternate mode that following byte is consequently the
			// lower reflection point.  Treating the anchor itself as the lower
			// endpoint adds it twice per round trip and creates a predictor walk.
			const uint32_t repeatAddress = loopAddress == endAddress
				? loopAddress : (loopAddress + firstStep) & 0x3ffff;

			auto advanceAddress = [alternateLoop, firstStep, repeatAddress, endAddress]
				(uint32_t& _address, bool& _returnLeg)
			{
				const uint32_t boundary = _returnLeg ? repeatAddress : endAddress;
				const bool atBoundary = _address == boundary;
				if (!alternateLoop && atBoundary)
					_address = repeatAddress;
				else if (!atBoundary)
					_address = (_address + (_returnLeg ? -firstStep : firstStep)) & 0x3ffff;
				_returnLeg = alternateLoop && (_returnLeg != atBoundary);
			};

			std::array<int32_t, 5> deltas{};
			uint32_t address = highPhase;
			bool returnLeg = runtime.playDirection != 0;
			uint32_t nextAddress = address;
			bool nextReturnLeg = returnLeg;
			for (unsigned fetch = 0; fetch < 5; ++fetch)
			{
				deltas[fetch] = decodeSample(static_cast<int8_t>(readRom(romBank | address)));
				advanceAddress(address, returnLeg);
				if (crossedBytes == fetch + 1)
				{
					nextAddress = address;
					nextReturnLeg = returnLeg;
				}
			}

			const int32_t initialPredictor = predictor(static_cast<unsigned>(i));
			int32_t reference = initialPredictor;
			for (unsigned crossed = 0; crossed < crossedBytes; ++crossed)
				reference = wrap12(reference + deltas[crossed]);
			int32_t sample = initialPredictor;
			for (unsigned tap = 0; tap < 3; ++tap)
				sample += (interpolationWeight(tap, interpolationPhase) * deltas[tap]) >> 12;
			setPredictor(static_cast<unsigned>(i), reference);

			setCurrentAddress(static_cast<unsigned>(i), (nextAddress << 14) | (subPhase & 0x3fff));
			runtime.playDirection = static_cast<int8_t>(nextReturnLeg);

			const int32_t target = envelopeLevel(envelopeTarget);
			const int32_t increment = envelopeIncrement(envelopeRate);
			if (envelopeRate == 0xff)
			{
				volume = target;
				runtime.envelopeActive = false;
			}
			const bool increasing = volume < target;
			const bool decreasing = volume > target;
			const bool segmentActive = runtime.envelopeActive;
			if (increasing || decreasing)
			{
				volume += increasing ? increment : -increment;
				const bool overshot = (increasing && volume >= target) || (decreasing && volume <= target);
				if (overshot)
				{
					volume = target;
					runtime.envelopeActive = false;
					signalEnvelopeComplete(static_cast<uint8_t>(i));
				}
				else runtime.envelopeActive = true;
			}
			else
			{
				runtime.envelopeActive = false;
				if (segmentActive) signalEnvelopeComplete(static_cast<uint8_t>(i));
			}
			setCurrentVolume(static_cast<unsigned>(i), volume);
			result[i] = static_cast<int32_t>((int64_t(sample) * (volume >> 10)) >> 10);
		}
		return result;
	}

	int16_t LP::decodeSample(const int8_t _data)
	{
		const int sign = _data < 0 ? -1 : 1;
		int value = _data < 0 ? -_data : _data;
		const uint8_t shift = static_cast<uint8_t>(value >> 4);
		value &= 0x0f;
		return static_cast<int16_t>(sign * (shift ? (0x10 + value) << (shift - 1) : value));
	}

	int32_t LP::envelopeLevel(const uint8_t _value)
	{
		if (_value < 0x30) return 0;
		const uint32_t mantissa = 0x10 | (_value & 0x0f);
		const int shift = int(_value >> 4) - 7;
		const uint32_t units = shift >= 0 ? mantissa << shift : mantissa >> -shift;
		return static_cast<int32_t>(units << 13);
	}

	int32_t LP::envelopeIncrement(const uint8_t _value)
	{
		if (_value == 0) return 0;
		if (_value < 0x80)
			return static_cast<int32_t>((8u | (_value & 7u)) << (3u + (_value >> 3)));
		const unsigned group = (_value - 0x80) >> 3;
		const unsigned mantissa = 16u - (_value & 7u);
		return group < 15 ? static_cast<int32_t>(mantissa << (14u - group))
			: static_cast<int32_t>(8u - (_value & 7u));
	}

	int32_t LP::interpolationWeight(const unsigned _tap, const uint8_t _phase)
	{
		return _tap < InterpolationRom.size() ? InterpolationRom[_tap][_phase & 0x7f] : 0;
	}

	uint8_t LP::readRom(const uint32_t _address) const
	{
		return _address < m_rom.size() ? m_rom[_address] : 0xff;
	}

	unsigned LP::activeVoiceCount() const
	{
		return static_cast<unsigned>(std::count_if(m_voiceRuntime.begin(), m_voiceRuntime.end(),
			[](const VoiceRuntime& _voice) { return _voice.enabled; }));
	}

	LP::VoiceState LP::voiceState(const unsigned _voice) const
	{
		if (_voice >= VoiceCount) return {};
		const uint16_t bankVolume = parameterWord(_voice, 1);
		const uint16_t envelope = parameterWord(_voice, 3);
		return {currentVolume(_voice), static_cast<uint8_t>((bankVolume >> 8) & 0xfc), parameterWord(_voice, 2),
			static_cast<uint8_t>(envelope), static_cast<uint8_t>(envelope >> 8), currentAddress(_voice),
			parameterWord(_voice, 6), parameterWord(_voice, 7), predictor(_voice), m_voiceRuntime[_voice].enabled};
	}

	void LP::signalEnvelopeComplete(const uint8_t _voice)
	{
		if (!m_irqCurrentValid)
		{
			m_irqVoice = _voice;
			m_irqCurrentValid = true;
			setInterruptLine(false);
			setInterruptLine(true);
		}
		else if (m_irqCount < VoiceCount)
		{
			m_irqQueue[m_irqWrite] = _voice;
			m_irqWrite = static_cast<uint8_t>((m_irqWrite + 1) % VoiceCount);
			++m_irqCount;
		}
	}

	void LP::setInterruptLine(const bool _level)
	{
		if (m_interrupt) m_interrupt(_level);
	}
}
