#pragma once

#include "xp_dsp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace xpLib
{
	class XP
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		enum class PhysicalWaveRomWidth : uint8_t
		{
			bits8,
			bits16,
		};

		static constexpr size_t nMaxVoices = 64;
		static constexpr size_t nWaveChipSelects = 8;
		static constexpr size_t nDspSlots = Dsp::nProgramSlots;
		static constexpr size_t nIramSlots = Dsp::nIramSlots;
		static constexpr size_t nEramWords = Dsp::nEramWords;
		static constexpr int32_t outputFullScale = 1 << 21;
		static int32_t serialWordToOutput(const int32_t _word)
		{
			return _word >= 0 ? _word / 4 : -static_cast<int32_t>((-int64_t{_word} + 3) / 4);
		}

		struct WaveControl
		{
			static constexpr uint32_t bankMask = 0x0000f;
			static constexpr uint32_t chipSelectMask = 0x00070;
			static constexpr uint32_t fceDpcmExponent0OrProducerLive = 0x00080;
			static constexpr uint32_t ldpcmProducerModifier = 0x00100;
			static constexpr uint32_t ldpcmFormat = 0x00200;
			static constexpr uint32_t waveEngineHalt = 0x00400;
			static constexpr uint32_t reverse = 0x00800;
			static constexpr uint32_t alternateLoop = 0x01000;
			static constexpr uint32_t directionState = 0x02000;
			static constexpr uint32_t loopReason6 = 0x04000;
			static constexpr uint32_t irqEnable = 0x08000;
			static constexpr uint32_t loopEventInhibit = 0x10000;
			static constexpr uint32_t loopMarkerFirstStage = 0x20000;
			static constexpr uint32_t loopStatus = 0x30000;
			static constexpr uint32_t muteStatus = 0x40000;
			static constexpr uint32_t muteRequest = 0x80000;
			static constexpr uint32_t producerBlocked = 0xc0000;
		};

		struct IrqReason
		{
			static constexpr uint8_t tvfQTerminal = 0;
			static constexpr uint8_t tvfFTerminal = 1;
			static constexpr uint8_t pitchTerminal = 2;
			static constexpr uint8_t ampModTerminal = 3;
			static constexpr uint8_t ampTerminal = 4;
			static constexpr uint8_t playbackMarker = 5;
			static constexpr uint8_t alternatePlaybackMarker = 6;
			static constexpr uint8_t globalWaveFetchPressure = 7;
			static constexpr uint8_t muteTransition = 8;
		};

		struct FilterConfig
		{
			static constexpr uint32_t pairRole = 0x00010;
			static constexpr uint32_t boosterMask = 0x000c0;
			static constexpr uint32_t pairedSecondModeMask = 0x00300;
			static constexpr uint32_t modeMask = 0x00c00;
			static constexpr uint32_t structureMask = 0x0f000;
		};

		struct HostAddress
		{
			static constexpr uint16_t waveControl_0000 = 0x0000;
			static constexpr uint16_t sampleCurrent_0100 = 0x0100;
			static constexpr uint16_t sampleLoop_0200 = 0x0200;
			static constexpr uint16_t sampleEnd_0300 = 0x0300;
			static constexpr uint16_t waveFetchState_0400 = 0x0400;
			static constexpr uint16_t waveBuffer_0800 = 0x0800;
			static constexpr uint16_t dpcmAccumulator_0c00 = 0x0c00;
			static constexpr uint16_t pitchIncrement_0d00 = 0x0d00;
			static constexpr uint16_t addressFraction_0e00 = 0x0e00;
			static constexpr uint16_t playbackStateConfig_1000 = 0x1000;
			static constexpr uint16_t tvfQDestination_1100 = 0x1100;
			static constexpr uint16_t pitchDestination_1200 = 0x1200;
			static constexpr uint16_t tvfFDestination_1300 = 0x1300;
			static constexpr uint16_t ampModDestination_1400 = 0x1400;
			static constexpr uint16_t ampDestination_1500 = 0x1500;
			static constexpr uint16_t tvfQRamp_1600 = 0x1600;
			static constexpr uint16_t pitchRamp_1700 = 0x1700;
			static constexpr uint16_t tvfFRamp_1800 = 0x1800;
			static constexpr uint16_t ampModRamp_1900 = 0x1900;
			static constexpr uint16_t ampRamp_1a00 = 0x1a00;
			static constexpr uint16_t pitchCurrent_1b00 = 0x1b00;
			static constexpr uint16_t tvfFCurrent_1c00 = 0x1c00;
			static constexpr uint16_t ampModCurrent_1d00 = 0x1d00;
			static constexpr uint16_t ampCurrent_1e00 = 0x1e00;
			static constexpr uint16_t filterConfig_2000 = 0x2000;
			static constexpr uint16_t tvfQCurrent_2100 = 0x2100;
			static constexpr uint16_t tvfFCoefficient_2200 = 0x2200;
			static constexpr uint16_t combinedAmp_2300 = 0x2300;
			static constexpr uint16_t pitchStep_2400 = 0x2400;
			static constexpr uint16_t tvfFStep_2500 = 0x2500;
			static constexpr uint16_t ampStep_2600 = 0x2600;
			static constexpr uint16_t tvaGain_2700 = 0x2700;
			static constexpr uint16_t filterBp_2800 = 0x2800;
			static constexpr uint16_t filterLp_2900 = 0x2900;
			static constexpr uint16_t filterOutput_2a00 = 0x2a00;
			static constexpr uint16_t cram_2c00 = 0x2c00;
			static constexpr uint16_t iram1_3000 = 0x3000;
			static constexpr uint16_t iram2_3100 = 0x3100;
			static constexpr uint16_t iram3_3200 = 0x3200;
			static constexpr uint16_t iram3Targets_3300 = 0x3300;
			static constexpr uint16_t pram_3400 = 0x3400;
			static constexpr uint16_t voiceReset_3900 = 0x3900;
			static constexpr uint16_t waveRomConfig_3908 = 0x3908;
			static constexpr uint16_t readbackLow_3910 = 0x3910;
			static constexpr uint16_t readbackHigh_3912 = 0x3912;
			static constexpr uint16_t highestVoice_3914 = 0x3914;
			static constexpr uint16_t dspControl_3916 = 0x3916;
			static constexpr uint16_t irqStatusConfig_3918 = 0x3918;
			static constexpr uint16_t irqAcknowledge_391a = 0x391a;
			static constexpr uint16_t readbackStatus_391c = 0x391c;
			static constexpr uint16_t waveRomPage_3920 = 0x3920;
			static constexpr uint16_t waveRomBank_3922 = 0x3922;
			static constexpr uint16_t serialAudio0_3924 = 0x3924;
			static constexpr uint16_t serialAudio1_3926 = 0x3926;
			static constexpr uint16_t iram3RampRates_3928 = 0x3928;
			static constexpr uint16_t diagnosticSelect_3930 = 0x3930;
			static constexpr uint16_t serialFormat_3932 = 0x3932;
			static constexpr uint16_t voiceWindowSelect_3934 = 0x3934;
			static constexpr uint16_t voiceWindow_3940 = 0x3940;
			static constexpr uint16_t voiceWindowEnd_39f0 = 0x39f0;
			static constexpr uint16_t voiceMixerWindow_39f8 = 0x39f8;
			static constexpr uint16_t voiceMixerWindowEnd_3a00 = 0x3a00;
			static constexpr uint16_t mixerA_3a00 = 0x3a00;
			static constexpr uint16_t waveRomAperture_3c00 = 0x3c00;
		};

		enum class VoiceRuntimePhaseCache : uint8_t
		{
			parked,
			preload,
			initialize,
			starting,
			running,
		};

		enum class WaveSampleFormatCache : uint8_t
		{
			fceDpcm, // XP default: signed mantissa plus ROM exponent nibble.
			fceDpcmExponent0, // Bit-7 launch mode: FCE-DPCM with an implicit zero exponent.
			ldpcm, // Bit-9 launch mode: LP-compatible nonlinear signed-byte DPCM.
		};

		struct VoiceRuntimeCache
		{
			// Verified launch/host-interface pipeline state, not additional XP RAM.
			bool ampCurve2EntryPending = false; // An unheld curve-2 write awaits its amp-envelope service.
			VoiceRuntimePhaseCache runtimePhase = VoiceRuntimePhaseCache::parked; // Substates around 0x1000 bits 16-17.
			WaveSampleFormatCache waveSampleFormat = WaveSampleFormatCache::fceDpcm; // Launch-latched 0x0000 bits 7/9.
		};

		struct VoiceResetState
		{
			// Known 0x3900-0x3906 state: the last written bit and the state committed by a mask read.
			bool released = false;
			bool shadow = false;
		};

		struct VoiceState
		{
			// Every member except the explicitly named runtime cache is storage exposed by the host bank in its suffix.
			uint32_t waveControl_0000 = 0;
			uint32_t sampleCurrent_0100 = 0;
			uint32_t sampleLoop_0200 = 0;
			uint32_t sampleEnd_0300 = 0;
			uint32_t waveFetchState_0400 = 0;
			std::array<uint16_t, 8> waveCircularBuffer_0800{};
			uint32_t dpcmAccumulator_0c00 = 0;
			uint32_t pitchIncrement_0d00 = 0;
			uint32_t addressFraction_0e00 = 0;
			uint32_t playbackStateConfig_1000 = 0;
			uint32_t tvfQDestination_1100 = 0;
			uint32_t pitchDestination_1200 = 0;
			uint32_t tvfFDestination_1300 = 0;
			uint32_t ampModDestination_1400 = 0;
			uint32_t ampDestination_1500 = 0;
			uint32_t tvfQRamp_1600 = 0;
			uint32_t pitchRamp_1700 = 0;
			uint32_t tvfFRamp_1800 = 0;
			uint32_t ampModRamp_1900 = 0;
			uint32_t ampRamp_1a00 = 0;
			uint32_t pitchCurrent_1b00 = 0;
			uint32_t tvfFCurrent_1c00 = 0;
			uint32_t ampModCurrent_1d00 = 0;
			uint32_t ampCurrent_1e00 = 0;
			uint32_t filterConfig_2000 = 0;
			uint32_t tvfQCurrent_2100 = 0;
			uint32_t tvfFCoefficient_2200 = 0;
			uint32_t combinedAmp_2300 = 0;
			uint32_t pitchStep_2400 = 0;
			uint32_t tvfFStep_2500 = 0;
			uint32_t ampStep_2600 = 0;
			uint16_t tvaGain_2700 = 0;
			uint32_t filterBp_2800 = 0;
			uint32_t filterLp_2900 = 0;
			uint32_t filterOutput_2a00 = 0;
			std::array<uint16_t, 4> mixer_3a00{};
			VoiceResetState resetState_3900{};
			VoiceRuntimeCache runtimeCache{};
		};

		struct State
		{
			std::array<VoiceState, nMaxVoices> voices{};
			Dsp dsp{};

			uint32_t readbackLatch = 0;
			uint16_t wideWriteLatch = 0;
			uint16_t highestVoice = nMaxVoices - 1;
			uint16_t irqStatus = 0;
			uint16_t irqConfigMask = 0;
			uint16_t irqAcknowledge = 0;
			// Diagnostic indication that at least one producer was blocked during the
			// current complete sample-frame step.
			bool irqBlockedEvent = false;
			// Raw per-CS descriptors: b0 selects 8/16-bit fetching; b1 changes sample decoding.
			std::array<uint8_t, nWaveChipSelects> waveRomConfig{};
			uint16_t waveRomPage = 0;
			uint16_t waveRomBank = 0;
			std::array<uint16_t, 2> serialAudioConfig{};
			uint16_t diagnosticSelect_3930 = 0;
			// 0x3932: bit 5 selects physical B/C/D packing on XP3; other positive roles are unknown.
			uint16_t serialFormat_3932 = 0;
			uint8_t voiceWindowSelect_3934 = 0; // Write-only selector for the transposed 0x3940-0x39ef window.
			uint16_t dspControl = 0;
			std::array<uint16_t, 4> iram3RampRates{};
			uint64_t sampleClock = 0; // Free-running control/ramp phase; voice reset does not stop it.
			bool interrupt = false;
		};

		XP();

		uint16_t hostRead(uint16_t _address);
		void hostWrite(uint16_t _address, uint16_t _value);
		uint8_t hostRead8(uint16_t _address);
		void hostWrite8(uint16_t _address, uint8_t _value);
		// Hardware power-on reset. ROM mappings and
		// the interrupt callback are board wiring, so they remain installed.
		void reset();
		void step();
		// Step a pair of XPs sharing their synchronous SDOA/SDIA link. Voice and
		// mixer work is prepared on both chips first, then their DSPs advance in
		// lockstep so same-frame serial words are observable by the peer.
		static void stepLinked(XP& _a, XP& _b);
		void setInterruptCallback(std::function<void(bool)> _callback) { m_interruptCallback = std::move(_callback); }
		const std::function<void(bool)>& interruptCallback() const { return m_interruptCallback; }

		bool interruptState() const { return m_state.interrupt; }
		Dsp& dsp() { return m_state.dsp; }
		const Dsp& dsp() const { return m_state.dsp; }
		State& state() { return m_state; }
		const State& state() const { return m_state; }
		// The caller owns the mapped ROM data and must keep it alive.
		void mapWaveRom(size_t _chipSelect, const uint8_t* _data, size_t _size, PhysicalWaveRomWidth _width,
						uint8_t _apertureBankShift = 0, uint8_t _voiceBankShift = 0);

	private:
		struct WaveRomView
		{
			const uint8_t* data = nullptr;
			size_t size = 0;
			PhysicalWaveRomWidth width = PhysicalWaveRomWidth::bits8;
			// The host aperture and voice descriptor have distinct bank encodings
			// on the SC-8850: 3922 uses 0/4/8/C, while voice control uses 0/1/2/3.
			uint8_t apertureBankShift = 0;
			uint8_t voiceBankShift = 0;
		};

		struct WideRegister
		{
			uint32_t* value = nullptr;
			uint8_t width = 0;
		};

		struct VoiceStepResult
		{
			int32_t sample = 0;
			bool filterDue = false;
		};

		using MixerFrame = Dsp::MixerFrame;

		WideRegister findVoiceWideRegister(uint16_t _address);
		WideRegister findDspWideRegister(uint16_t _address);
		uint16_t translateVoiceWindowAddress(uint16_t _address) const;
		uint16_t translateVoiceMixerWindowAddress(uint16_t _address) const;
		uint16_t hostReadInternal(uint16_t _address);
		void hostWriteInternal(uint16_t _address, uint16_t _value);
		void updateInterruptLine();
		void writeWide(const WideRegister& _reg, uint16_t _address, uint16_t _value);
		uint16_t readWide(const WideRegister& _reg, uint16_t _address);
		void writeReleaseMask(size_t _word, uint16_t _value);
		void commitReleasedVoices();
		VoiceStepResult stepVoiceSource(size_t _voiceIndex, VoiceState& _voice);
		void stepVoiceRamps(size_t _voiceIndex, VoiceState& _voice);
		bool acceptVoiceEvent(size_t _voiceIndex, uint8_t _reason);
		void raiseVoiceRampTerminalEvent(size_t _voiceIndex, uint32_t& _control, uint8_t _reason);
		void raiseVoiceMuteEvent(size_t _voiceIndex, VoiceState& _voice);
		uint8_t readVoiceWaveByte(const VoiceState& _voice, uint32_t _address) const;
		uint16_t readVoiceSampleFromCache(const VoiceState& _voice, uint32_t _address) const;
		uint16_t readExponentCache(const VoiceState& _voice, uint32_t _address) const;
		void refreshExponentCacheAfterAdvance(VoiceState& _voice, bool _looped);
		bool advanceVoiceAddress(size_t _voiceIndex, VoiceState& _voice);
		void raiseVoiceLoopMarkerEvent(size_t _voiceIndex, VoiceState& _voice);
		bool voiceAddressRunsReverse(const VoiceState& _voice) const;
		bool voiceUses16BitWaveBus(const VoiceState& _voice) const;
		uint16_t encodeVoiceSample(const VoiceState& _voice, uint8_t _mantissa, uint8_t _exponent) const;
		int32_t decodeVoiceDelta(const VoiceState& _voice, uint16_t _encoded) const;
		int32_t interpolateVoice(const VoiceState& _voice, uint32_t _phase) const;
		int32_t stepVoiceFilter(VoiceState& _voice, int32_t _input, unsigned _mode);
		void stepPairedVoiceFilter(VoiceState& _owner, VoiceState& _partner, int32_t _ownerInput,
								   int32_t _partnerInput);
		int32_t applyTva(const VoiceState& _voice, int32_t _input) const;
		void stepTvaGain(VoiceState& _voice);
		// Voice stage of one sample frame: fills m_frame / m_frameVoices.
		void prepareSample();
		Dsp::StepRequest dspStepRequest() const;
		void finishSample();
		void prepareMixer(size_t _voiceCount);
		void refillVoiceCell(size_t _voiceIndex, VoiceState& _voice);
		void refillVoicePair(size_t _voiceIndex, VoiceState& _voice);
		uint16_t readWaveRom(uint16_t _address);

		State m_state{};
		MixerFrame m_frame{};
		size_t m_frameVoices = 0;
		DspMixerSummary m_mixerSummary; // the current frame's sends summed per cell, for Dsp::hoistDeposits
		std::array<WaveRomView, nWaveChipSelects> m_waveRoms{};
		std::array<uint8_t, 0x4000> m_hostWriteBytes{};
		uint16_t m_hostReadWord = 0;
		uint16_t m_hostReadAddress = 0xffff;
		bool m_interruptLine = false;
		std::function<void(bool)> m_interruptCallback;
		bool m_irqEventAcceptedThisStep = false;
	};
} // namespace xpLib
