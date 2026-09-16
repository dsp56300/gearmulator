#include "xp.h"
#include "xp_voice_common.h"

namespace xpLib
{
	using namespace xpInternal;

	XP::WideRegister XP::findVoiceWideRegister(const uint16_t _address)
	{
		const auto bank = static_cast<uint16_t>(_address & 0xff00);
		const auto voice = static_cast<size_t>(_address & 0x00ff) / voiceWideStride;
		if (voice >= nMaxVoices)
			return {};

		auto& v = m_state.voices[voice];
		switch (bank)
		{
		case Address::waveControl_0000:
			return {&v.waveControl_0000, 20};
		case Address::sampleCurrent_0100:
			return {&v.sampleCurrent_0100, 20};
		case Address::sampleLoop_0200:
			return {&v.sampleLoop_0200, 20};
		case Address::sampleEnd_0300:
			return {&v.sampleEnd_0300, 20};
		case Address::waveFetchState_0400:
			return {&v.waveFetchState_0400, 20};
		case Address::dpcmAccumulator_0c00:
			return {&v.dpcmAccumulator_0c00, 18};
		case Address::pitchIncrement_0d00:
			return {&v.pitchIncrement_0d00, 18};
		case Address::addressFraction_0e00:
			return {&v.addressFraction_0e00, 18};
		case Address::playbackStateConfig_1000:
			return {&v.playbackStateConfig_1000, 18};
		case Address::tvfQDestination_1100:
			return {&v.tvfQDestination_1100, 18};
		case Address::pitchDestination_1200:
			return {&v.pitchDestination_1200, 18};
		case Address::tvfFDestination_1300:
			return {&v.tvfFDestination_1300, 18};
		case Address::ampModDestination_1400:
			return {&v.ampModDestination_1400, 18};
		case Address::ampDestination_1500:
			return {&v.ampDestination_1500, 18};
		case Address::tvfQRamp_1600:
			return {&v.tvfQRamp_1600, 18};
		case Address::pitchRamp_1700:
			return {&v.pitchRamp_1700, 18};
		case Address::tvfFRamp_1800:
			return {&v.tvfFRamp_1800, 18};
		case Address::ampModRamp_1900:
			return {&v.ampModRamp_1900, 18};
		case Address::ampRamp_1a00:
			return {&v.ampRamp_1a00, 18};
		case Address::pitchCurrent_1b00:
			return {&v.pitchCurrent_1b00, 18};
		case Address::tvfFCurrent_1c00:
			return {&v.tvfFCurrent_1c00, 18};
		case Address::ampModCurrent_1d00:
			return {&v.ampModCurrent_1d00, 18};
		case Address::ampCurrent_1e00:
			return {&v.ampCurrent_1e00, 18};
		case Address::filterConfig_2000:
			return {&v.filterConfig_2000, 20};
		case Address::tvfQCurrent_2100:
			return {&v.tvfQCurrent_2100, 20};
		case Address::tvfFCoefficient_2200:
			return {&v.tvfFCoefficient_2200, 20};
		case Address::combinedAmp_2300:
			return {&v.combinedAmp_2300, 20};
		case Address::pitchStep_2400:
			return {&v.pitchStep_2400, 20};
		case Address::tvfFStep_2500:
			return {&v.tvfFStep_2500, 20};
		case Address::ampStep_2600:
			return {&v.ampStep_2600, 20};
		case Address::filterBp_2800:
			return {&v.filterBp_2800, 24};
		case Address::filterLp_2900:
			return {&v.filterLp_2900, 24};
		case Address::filterOutput_2a00:
			return {&v.filterOutput_2a00, 24};
		default:
			return {};
		}
	}

	XP::WideRegister XP::findDspWideRegister(const uint16_t _address)
	{
		if (_address >= Address::iram1_3000 && _address < iramEnd)
		{
			const auto bank = static_cast<size_t>((_address - Address::iram1_3000) >> 8);
			const auto slot = static_cast<size_t>(_address & 0x00ff) / voiceWideStride;
			if (slot >= nIramSlots)
				return {};

			switch (bank)
			{
			case 0:
				return {&m_state.dsp.iram1()[slot], 24};
			case 1:
				return {&m_state.dsp.iram2()[slot], 24};
			case 2:
				return {&m_state.dsp.iram3()[slot], 26};
			default:
				return {};
			}
		}

		if (_address >= Address::pram_3400 && _address < pramEnd)
		{
			const auto slot = static_cast<size_t>(_address - Address::pram_3400) / voiceWideStride;
			if (slot < nDspSlots)
				return {&m_state.dsp.pram()[slot], 28};
		}
		return {};
	}

	uint16_t XP::translateVoiceWindowAddress(const uint16_t _address) const
	{
		const auto offset = static_cast<uint16_t>(_address - Address::voiceWindow_3940);
		const auto bank = static_cast<uint16_t>((offset >> 2) << 8);
		const auto half = static_cast<uint16_t>(offset & 2);
		return static_cast<uint16_t>(bank | (m_state.voiceWindowSelect_3934 * voiceWideStride) | half);
	}

	uint16_t XP::translateVoiceMixerWindowAddress(const uint16_t _address) const
	{
		const auto send = static_cast<uint16_t>((_address - Address::voiceMixerWindow_39f8) >> 1);
		return static_cast<uint16_t>(Address::mixerA_3a00 + send * 0x80 +
									 m_state.voiceWindowSelect_3934 * voiceNarrowStride);
	}

	void XP::writeWide(const WideRegister& _reg, const uint16_t _address, const uint16_t _value)
	{
		if ((_address & 2) == 0)
		{
			m_state.wideWriteLatch = _value;
			return;
		}
		*_reg.value = ((static_cast<uint32_t>(m_state.wideWriteLatch) << 16) | _value) & widthMask(_reg.width);
	}

	uint16_t XP::readWide(const WideRegister& _reg, const uint16_t _address)
	{
		if ((_address & 2) == 0)
			return 0;
		m_state.readbackLatch = *_reg.value & widthMask(_reg.width);
		return 0;
	}

	uint16_t XP::hostRead(const uint16_t _address)
	{
		const auto result = hostReadInternal(_address);
		updateInterruptLine();
		return result;
	}

	uint8_t XP::hostRead8(const uint16_t _address)
	{
		const auto address = static_cast<uint16_t>(_address & 0x3fff);
		// A physical 8-bit wave ROM launches a distinct aperture transaction for
		// every byte address. JV firmware uses mov.b here and then collects the
		// result from 0x3910, so folding an odd address onto the preceding word
		// loses ROM A0. A physical 16-bit ROM still uses one aligned transaction:
		// boards with an 8-bit CPU bus present that word as consecutive even/odd
		// host reads, and the odd byte must not launch a second, shifted ROM read.
		if (address >= Address::waveRomAperture_3c00 && address < waveRomApertureEnd)
		{
			const auto chipSelect = static_cast<size_t>((m_state.waveRomBank >> 4) & 7);
			if (m_waveRoms[chipSelect].width == PhysicalWaveRomWidth::bits8)
			{
				readWaveRom(address);
				m_hostReadAddress = 0xffff;
				return 0;
			}
		}
		const auto wordAddress = static_cast<uint16_t>(address & ~uint16_t{1});
		if ((address & 1) == 0 || m_hostReadAddress != wordAddress)
		{
			m_hostReadWord = hostRead(wordAddress);
			m_hostReadAddress = wordAddress;
		}
		if ((address & 1) == 0)
			return static_cast<uint8_t>(m_hostReadWord >> 8);

		m_hostReadAddress = 0xffff;
		return static_cast<uint8_t>(m_hostReadWord);
	}

	uint16_t XP::hostReadInternal(const uint16_t _address)
	{
		if ((_address & 1) != 0)
		{
			return 0;
		}

		if (_address >= Address::voiceWindow_3940 && _address < Address::voiceWindowEnd_39f0)
			return hostReadInternal(translateVoiceWindowAddress(_address));
		if (_address >= Address::voiceMixerWindow_39f8 && _address < Address::voiceMixerWindowEnd_3a00)
			return hostReadInternal(translateVoiceMixerWindowAddress(_address));

		if (const auto reg = findVoiceWideRegister(_address); reg.value != nullptr)
			return readWide(reg, _address);

		if (_address >= Address::waveBuffer_0800 && _address < waveBufferEnd)
		{
			const auto offset = static_cast<size_t>(_address - Address::waveBuffer_0800);
			const auto cell = offset >> 7;
			const auto voice = (offset & 0x7f) / voiceNarrowStride;
			m_state.readbackLatch = m_state.voices[voice].waveCircularBuffer_0800[cell] & 0x0fff;
			return 0;
		}

		if (_address >= Address::tvaGain_2700 && _address < tvaGainEnd)
		{
			// RAM O is 64x16 but uses the ordinary four-byte voice slot. Its
			// only readable half is the low word at slot+2; a base read does
			// not launch a latch transfer.
			if ((_address & 2) != 0)
			{
				const auto voice = static_cast<size_t>(_address - Address::tvaGain_2700) / voiceWideStride;
				m_state.readbackLatch = m_state.voices[voice].tvaGain_2700;
			}
			return 0;
		}

		if (const auto reg = findDspWideRegister(_address); reg.value != nullptr)
			return readWide(reg, _address);

		if (_address >= Address::cram_2c00 && _address < cramEnd)
		{
			const auto slot = static_cast<size_t>(_address - Address::cram_2c00) / voiceNarrowStride;
			m_state.readbackLatch = m_state.dsp.cram()[slot];
			return 0;
		}

		if (_address >= Address::iram3Targets_3300 && _address < iram3TargetsEnd)
		{
			m_state.readbackLatch = 0;
			return 0;
		}

		if (_address >= Address::mixerA_3a00 && _address < mixerEnd)
		{
			const auto offset = static_cast<size_t>(_address - Address::mixerA_3a00);
			const auto mixerIndex = offset >> 7;
			const auto voice = (offset & 0x7f) / voiceNarrowStride;
			m_state.readbackLatch = m_state.voices[voice].mixer_3a00[mixerIndex];
			return 0;
		}

		if (_address >= Address::waveRomAperture_3c00 && _address < waveRomApertureEnd)
			return readWaveRom(_address);

		switch (_address)
		{
		case Address::voiceReset_3900:
		case Address::voiceReset_3900 + 2:
		case Address::voiceReset_3900 + 4:
		case Address::voiceReset_3900 + 6:
			commitReleasedVoices();
			return 0;
		case Address::waveRomConfig_3908:
		case Address::waveRomConfig_3908 + 2:
		case Address::waveRomConfig_3908 + 4:
		case Address::waveRomConfig_3908 + 6:
			{
				const auto cs = static_cast<size_t>(_address - Address::waveRomConfig_3908);
				return static_cast<uint16_t>(m_state.waveRomConfig[cs]) |
					(static_cast<uint16_t>(m_state.waveRomConfig[cs + 1]) << 8);
			}
		case Address::readbackLow_3910:
			return static_cast<uint16_t>(m_state.readbackLatch);
		case Address::readbackHigh_3912:
			return static_cast<uint16_t>(m_state.readbackLatch >> 16);
		case Address::highestVoice_3914:
			return m_state.highestVoice;
		case Address::dspControl_3916:
			return 0;
		case Address::irqStatusConfig_3918:
			return m_state.irqStatus;
		case Address::irqAcknowledge_391a:
			m_state.interrupt = false;
			return m_state.irqAcknowledge;
		case Address::readbackStatus_391c:
			// Busy bit 7 is firmware-visible, but the host transaction duration is not yet cycle-modelled.
			return (m_state.diagnosticSelect_3930 & 0x07ff) >= 0x05a0 ? 0x0040 : 0;
		case Address::waveRomPage_3920:
			return m_state.waveRomPage;
		case Address::waveRomBank_3922:
			return m_state.waveRomBank;
		case Address::serialAudio0_3924:
			return m_state.serialAudioConfig[0];
		case Address::serialAudio1_3926:
			return m_state.serialAudioConfig[1];
		case Address::voiceWindowSelect_3934:
			return 0;
		case Address::iram3RampRates_3928:
		case Address::iram3RampRates_3928 + 2:
		case Address::iram3RampRates_3928 + 4:
		case Address::iram3RampRates_3928 + 6:
			return m_state
				.iram3RampRates[static_cast<size_t>(_address - Address::iram3RampRates_3928) / voiceNarrowStride];
		default:
			// Unmodeled host registers read as zero; hardware behavior is unverified.
			return 0;
		}
	}

	void XP::hostWrite(const uint16_t _address, const uint16_t _value)
	{
		hostWriteInternal(_address, _value);
		updateInterruptLine();
	}

	void XP::hostWrite8(const uint16_t _address, const uint8_t _value)
	{
		const auto address = static_cast<uint16_t>(_address & 0x3fff);
		m_hostWriteBytes[address] = _value;
		m_hostReadAddress = 0xffff;
		if ((address & 1) == 0)
			return;

		const auto wordAddress = static_cast<uint16_t>(address - 1);
		const auto value = static_cast<uint16_t>((static_cast<uint16_t>(m_hostWriteBytes[wordAddress]) << 8) | _value);
		hostWrite(wordAddress, value);
	}

	void XP::hostWriteInternal(const uint16_t _address, const uint16_t _value)
	{
		if ((_address & 1) != 0)
		{
			return;
		}

		if (_address >= Address::voiceWindow_3940 && _address < Address::voiceWindowEnd_39f0)
		{
			hostWriteInternal(translateVoiceWindowAddress(_address), _value);
			return;
		}
		if (_address >= Address::voiceMixerWindow_39f8 && _address < Address::voiceMixerWindowEnd_3a00)
		{
			hostWriteInternal(translateVoiceMixerWindowAddress(_address), _value);
			return;
		}

		if (const auto reg = findVoiceWideRegister(_address); reg.value != nullptr)
		{
			m_idleRetiredVoices &= ~(uint64_t{1} << ((_address & 0xff) / voiceWideStride));
			writeWide(reg, _address, _value);
			if ((_address & 0xff02) == (Address::ampRamp_1a00 | 2))
			{
				const auto voiceIndex = static_cast<size_t>(_address & 0x00ff) / voiceWideStride;
				const auto control = m_state.voices[voiceIndex].ampRamp_1a00;
				m_state.voices[voiceIndex].runtimeCache.ampCurve2EntryPending =
					(control & (rampCurveMask | rampHold)) == 0x08000;
			}
			return;
		}

		if (_address >= Address::waveBuffer_0800 && _address < waveBufferEnd)
		{
			const auto offset = static_cast<size_t>(_address - Address::waveBuffer_0800);
			const auto cell = offset >> 7;
			const auto voice = (offset & 0x7f) / voiceNarrowStride;
			m_state.voices[voice].waveCircularBuffer_0800[cell] = _value & 0x0fff;
			return;
		}

		if (_address >= Address::tvaGain_2700 && _address < tvaGainEnd)
		{
			if ((_address & 2) == 0)
				m_state.wideWriteLatch = _value;
			else
			{
				const auto voice = static_cast<size_t>(_address - Address::tvaGain_2700) / voiceWideStride;
				m_state.voices[voice].tvaGain_2700 = _value;
			}
			return;
		}

		if (_address >= Address::pram_3400 && _address < pramEnd)
		{
			// PRAM commits through the DSP so a changed word can be classified: an offset-only edit is
			// patched into the decoded program, anything else marks it for a full decode.
			const auto slot = static_cast<size_t>(_address - Address::pram_3400) / voiceWideStride;
			if (slot >= nDspSlots)
				return;
			if ((_address & 2) == 0)
				m_state.wideWriteLatch = _value;
			else
				m_state.dsp.writePram(slot, (static_cast<uint32_t>(m_state.wideWriteLatch) << 16) | _value);
			return;
		}

		if (const auto reg = findDspWideRegister(_address); reg.value != nullptr)
		{
			writeWide(reg, _address, _value);
			return;
		}

		if (_address >= Address::cram_2c00 && _address < cramEnd)
		{
			const auto slot = static_cast<size_t>(_address - Address::cram_2c00) / voiceNarrowStride;
			m_state.dsp.writeCram(slot, _value);
			return;
		}

		if (_address >= Address::iram3Targets_3300 && _address < iram3TargetsEnd)
		{
			const auto slot = static_cast<size_t>(_address - Address::iram3Targets_3300) / voiceNarrowStride;
			m_state.dsp.setIram3Target(slot, _value, Dsp::iram3PartitionCount(m_state.serialAudioConfig[1]));
			return;
		}

		if (_address >= Address::mixerA_3a00 && _address < mixerEnd)
		{
			const auto offset = static_cast<size_t>(_address - Address::mixerA_3a00);
			const auto mixerIndex = offset >> 7;
			const auto voice = (offset & 0x7f) / voiceNarrowStride;
			m_state.voices[voice].mixer_3a00[mixerIndex] = _value;
			return;
		}

		switch (_address)
		{
		case Address::voiceReset_3900:
		case Address::voiceReset_3900 + 2:
		case Address::voiceReset_3900 + 4:
		case Address::voiceReset_3900 + 6:
			writeReleaseMask(static_cast<size_t>(_address - Address::voiceReset_3900) / voiceNarrowStride, _value);
			return;
		case Address::waveRomConfig_3908:
		case Address::waveRomConfig_3908 + 2:
		case Address::waveRomConfig_3908 + 4:
		case Address::waveRomConfig_3908 + 6:
			{
				const auto cs = static_cast<size_t>(_address - Address::waveRomConfig_3908);
				m_state.waveRomConfig[cs] = static_cast<uint8_t>(_value);
				m_state.waveRomConfig[cs + 1] = static_cast<uint8_t>(_value >> 8);
				return;
			}
		case Address::highestVoice_3914:
			m_state.highestVoice = _value;
			return;
		case Address::dspControl_3916:
			m_state.dspControl = _value;
			return;
		case Address::irqStatusConfig_3918:
			m_idleRetiredVoices = 0;
			m_state.irqConfigMask = _value;
			return;
		case Address::waveRomPage_3920:
			m_state.waveRomPage = _value;
			return;
		case Address::waveRomBank_3922:
			m_state.waveRomBank = _value;
			return;
		case Address::serialAudio0_3924:
			m_state.serialAudioConfig[0] = _value;
			return;
		case Address::serialAudio1_3926:
			m_state.serialAudioConfig[1] = _value;
			return;
		case Address::iram3RampRates_3928:
		case Address::iram3RampRates_3928 + 2:
		case Address::iram3RampRates_3928 + 4:
		case Address::iram3RampRates_3928 + 6:
			m_state.iram3RampRates[static_cast<size_t>(_address - Address::iram3RampRates_3928) / voiceNarrowStride] =
				_value;
			return;
		case Address::diagnosticSelect_3930:
			m_state.diagnosticSelect_3930 = _value;
			return;
		case Address::serialFormat_3932:
			m_state.serialFormat_3932 = _value;
			return;
		case Address::voiceWindowSelect_3934:
			m_state.voiceWindowSelect_3934 = static_cast<uint8_t>(_value & 0x3f);
			return;
		default:
			// Writes to unmodeled host registers are ignored.
			return;
		}
	}

	void XP::mapWaveRom(const size_t _chipSelect, const uint8_t* _data, const size_t _size, PhysicalWaveRomWidth _width,
						const uint8_t _apertureBankShift, const uint8_t _voiceBankShift)
	{
		if (_chipSelect >= nWaveChipSelects)
		{
			return;
		}
		m_waveRoms[_chipSelect] = {
			_data,
			_size,
			_width,
			static_cast<uint8_t>(std::min(_apertureBankShift, uint8_t{3})),
			static_cast<uint8_t>(std::min(_voiceBankShift, uint8_t{3})),
		};
	}

	uint16_t XP::readWaveRom(const uint16_t _address)
	{
		const auto chipSelect = static_cast<size_t>((m_state.waveRomBank >> 4) & 7);
		const auto& rom = m_waveRoms[chipSelect];
		const auto highAddress = (static_cast<size_t>(m_state.waveRomBank & 0x0f) >> rom.apertureBankShift) << 20;
		const auto pageAddress = static_cast<size_t>(m_state.waveRomPage & 0x03ff) << 10;
		const auto windowAddress = static_cast<size_t>(_address - Address::waveRomAperture_3c00);
		const auto romAddress = highAddress | pageAddress | windowAddress;
		if (rom.data == nullptr)
		{
			// Unpopulated chip selects read zero.
			m_state.readbackLatch = 0;
			return 0;
		}
		const size_t readSize = rom.width == PhysicalWaveRomWidth::bits8 ? 1 : 2;
		if (romAddress + readSize > rom.size)
		{
			m_state.readbackLatch = 0;
			return 0;
		}

		m_state.readbackLatch = rom.data[romAddress];
		if (readSize == 2)
			m_state.readbackLatch |= static_cast<uint16_t>(rom.data[romAddress + 1]) << 8;
		return 0;
	}
}
