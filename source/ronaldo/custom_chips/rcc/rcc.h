#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rccLib
{
	class RCC
	{
	public:
		static constexpr size_t VoiceCount = 32;
		// Signed 18-bit LP samples.
		using Voices = std::array<int32_t, VoiceCount>;

		struct OutputFrame
		{
			// Signed 16-bit serializer loads in program order.
			std::array<int32_t, 8> serialLoadWords{};
		};

		void reset();
		uint8_t read(uint16_t _offset) const;
		void write(uint16_t _offset, uint8_t _data);
		uint16_t readBus(uint16_t _offset) const;
		// D0 or g217 enables paired lanes at offsets 0 and C.
		void writeBus(uint16_t _offset, uint16_t _data, bool _g217 = false);
		[[nodiscard]] std::optional<OutputFrame> processFrame(const Voices& _voices);
		void setDramWordMask(uint16_t _mask) { m_dramWordMask = _mask; }

	private:
		static constexpr size_t RamASize = 32;
		static constexpr size_t RamBSize = 256;
		static constexpr size_t DramSize = 65536;
		static constexpr uint32_t Word24Mask = 0xffffffu;
		static constexpr uint32_t Word29Mask = 0x1fffffffu;

		struct Gain
		{
			uint8_t coefficient = 0; // Bits 7:0, signed by the multiplier.
			bool shiftFour = false; // Bit 8.
		};
		struct Gains
		{
			std::array<std::array<Gain, 2>, 32> voice{};
			Gain voice28Base{};
			Gain voice28RoutedSum{};
			Gain mixBase{};
			Gain mixBaseUpdate{};
			std::array<Gain, 3> diffuserFeedback{};
			std::array<Gain, 3> diffuserInput{};
			Gain reverbDampedFeedback{};
			Gain reverbDampingHistory{};
			Gain reverbFeedbackBase{};
			Gain reverbFeedbackGain{};
			Gain reverbFilteredInput{};
			Gain reverbInput{};
			Gain reverbInputFirstMix{};
			Gain reverbInputHistory0{};
			Gain reverbInputHistoryMix{};
			std::array<Gain, 2> reverbMix{};
			std::array<std::array<Gain, 6>, 2> reverbReturn{};
			Gain reverbReturnGain1{};
			Gain reverbReturnWithDry0{};
			Gain reverbReturnWithSend1{};
			Gain reverbSendSeed0{};
			Gain reverbTailInput{};
			Gain reverbTailStorage1{};
			std::array<Gain, 3> tailFeedback{};
			std::array<Gain, 3> tailInput{};
			Gain chorusDelayInput{};
			Gain chorusInputFirstMix{};
			Gain chorusInputGain{};
			Gain chorusInputMix{};
			Gain chorusInputResidual{};
			std::array<Gain, 4> chorusPhase{};
			Gain chorusPhaseBase1{};
			Gain chorusPhaseFeedback2{};
			Gain chorusPhaseResidual1{};
			std::array<Gain, 2> chorusReturnGain{};
			std::array<Gain, 2> chorusReturnMix{};
			Gain chorusReturnWithInput0{};
			Gain chorusReturnWithSend1{};
			Gain chorusSendCapture1{};
			Gain chorusSendComplement0{};
			std::array<Gain, 2> chorusSendSeed{};
			std::array<Gain, 2> chorusToReverb{};
			std::array<Gain, 4> phaseFold{};
			std::array<Gain, 4> previousChorusReturn{};
			std::array<Gain, 2> previousChorusWithSend{};
			std::array<Gain, 2> rampCandidate{};
			Gain rampIncrement0{};
			std::array<Gain, 2> rampLimit{};
			std::array<Gain, 2> rampNext{};
			Gain rampRangeCapture1{};
			std::array<Gain, 8> output{};
			Gain outputBusSeed1{};
			Gain outputCapture1{};
			std::array<Gain, 8> outputSeed{};
			Gain auxDelayTransfer2{};
		};
		struct Routing
		{
			uint8_t voice28RoutedSumWrite{};
			std::array<std::array<uint8_t, 2>, 32> voiceRead{};
			std::array<std::array<uint8_t, 2>, 32> voiceWrite{};
			uint8_t mixBaseSample{};
			uint8_t mixBaseUpdateSample{};
			uint8_t mixBaseUpdateWrite{};
			std::array<uint8_t, 2> reverbDrySample{};
			std::array<uint8_t, 2> reverbMixWrite{};
			std::array<uint8_t, 2> reverbSendSample{};
			uint8_t reverbSendSeedWrite0{};
			uint8_t chorusCrossfeedSample{};
			std::array<uint8_t, 4> chorusFoldSource{};
			uint8_t chorusInputFirstMixWrite{};
			uint8_t chorusInputFirstSample{};
			uint8_t chorusInputMixWrite{};
			uint8_t chorusInputResidualWrite{};
			uint8_t chorusInputSample{};
			uint8_t chorusInputSecondSample{};
			std::array<uint8_t, 4> chorusPhaseBase{};
			uint8_t chorusPhaseBaseSample1{};
			uint8_t chorusPhaseFeedbackSample2{};
			uint8_t chorusPhaseFeedbackWrite2{};
			uint8_t chorusPhaseFoldWrite1{};
			std::array<uint8_t, 4> chorusPhaseOffsetSample{};
			uint8_t chorusPhaseResidualWrite1{};
			std::array<uint8_t, 4> chorusPhaseWrite{};
			std::array<uint8_t, 2> chorusReturnMixWrite{};
			std::array<uint8_t, 2> chorusReturnSendSample{};
			uint8_t chorusSendCaptureWrite1{};
			uint8_t chorusSendInput0{};
			std::array<uint8_t, 2> chorusSendSample{};
			std::array<uint8_t, 2> chorusSendSeedWrite{};
			std::array<uint8_t, 2> chorusToReverbSample{};
			std::array<uint8_t, 2> chorusToReverbWrite{};
			uint8_t previousChorusReturnSample2{};
			uint8_t previousChorusReturnWrite2{};
			std::array<uint8_t, 2> rampIncrementSample{};
			std::array<uint8_t, 2> rampLimitSample{};
			std::array<uint8_t, 2> rampNextWrite{};
			uint8_t rampRangeCaptureWrite1{};
			uint8_t rampRangeSample1{};
			std::array<uint8_t, 2> rampResetSample{};
			std::array<uint8_t, 2> rampState{};
			std::array<uint8_t, 8> outputBias{};
			uint8_t outputBusSample1{};
			uint8_t outputBusSeedWrite1{};
			uint8_t outputCaptureWrite1{};
			uint8_t outputSeedSample1{};
			std::array<uint8_t, 8> outputSeedWrite{};
			std::array<uint8_t, 8> outputSignal{};
			std::array<uint8_t, 2> auxDelayGainSample{};
			uint8_t auxDelaySource2{};
		};

		struct DelaySection { uint32_t internal; uint32_t output; };
		enum class PhaseFold { Positive, Negative };

		static uint32_t saturateResult(uint32_t _result29);
		static uint32_t multiply24x8(uint32_t _operand, uint8_t _coefficient);
		static uint32_t shiftProduct(uint32_t _product);
		static uint32_t accumulate(uint32_t _a24, uint32_t _formatted32, unsigned _carry);
		static uint32_t accumulateSaturated(uint32_t _a24, uint32_t _formatted32, unsigned _carry);
		static uint32_t alignProduct(uint32_t _product, bool _shiftFour);
		static uint32_t addProduct(uint32_t _addend, uint32_t _formatted);
		static uint32_t subtractProduct(uint32_t _addend, uint32_t _formatted);
		static uint32_t multiplyAdd(uint32_t _addend, uint32_t _input, uint8_t _coefficient, bool _shiftFour);
		static uint32_t multiplySubtract(uint32_t _addend, uint32_t _input, uint8_t _coefficient, bool _shiftFour);
		static uint32_t multiplyAdd(uint32_t _addend, uint32_t _input, const Gain& _gain);
		static uint32_t multiplySubtract(uint32_t _addend, uint32_t _input, const Gain& _gain);
		static uint32_t foldPhase(uint32_t _base, uint32_t _source, const Gain& _gain, PhaseFold _direction);
		static uint32_t wrapRamp(uint32_t _candidate, uint32_t _limit, uint32_t _reset, const Gain& _gain);
		static DelaySection processDelaySection(uint32_t _input, uint32_t _delayed,
			const Gain& _inputGain, const Gain& _feedbackGain);
		static uint32_t interpolate(uint32_t _phase, uint32_t _first, uint32_t _adjacent);
		static int32_t outputWord(uint32_t _feedback, uint8_t _configuration);
		static OutputFrame makeOutput(const std::array<uint32_t, 8>& _sums, uint8_t _configuration);

		void decodeParameter(uint8_t _address, uint32_t _word);
		using VoiceProducts = std::array<std::array<uint32_t, 2>, VoiceCount>;

		// Captures carried between the fixed program's stages; never persistent between frames.
		struct ProgramFrame
		{
			VoiceProducts voiceProducts{};
			uint32_t mixBase{};
			uint32_t output0Signal{};
			uint32_t output0Bias{};
			uint32_t output1Signal{};
			uint32_t reverbPredelaySample{};
			uint32_t output2Signal{};
			uint32_t output2Bias{};
			uint32_t output3Signal{};
			uint32_t output3Bias{};
			uint32_t output4Signal{};
			uint32_t output4Bias{};
			uint32_t chorusFold0Source{};
		};
		void runInputStage(ProgramFrame& frame, const Voices& voices);
		void runReverbStage(ProgramFrame& frame);
		OutputFrame runChorusStage(ProgramFrame& frame);

		VoiceProducts prepareVoices(const Voices& _voices) const;
		void mixVoices(const VoiceProducts& _products, size_t _first, size_t _end);
		void writeVoicePair(const std::array<uint32_t, 2>& _inputs, const std::array<uint32_t, 2>& _products,
			const std::array<uint8_t, 2>& _destinations);
		uint32_t mixReverbReturn(uint32_t _base, const std::array<uint32_t, 5>& _samples,
			const std::array<Gain, 6>& _gains) const;
		OutputFrame mixOutputs(const std::array<uint32_t, 8>& _biases,
			const std::array<uint32_t, 8>& _signals) const;
		uint32_t readChorusTap(uint32_t _phase) const;
		std::array<uint32_t, 2> readChorusPair(const std::array<uint32_t, 2>& _phases) const;
		uint32_t readDram(uint32_t _address) const;
		uint32_t readDelay(uint32_t _offset) const;
		void writeDelay(uint32_t _offset, uint32_t _sample);
		void loadReadback(uint32_t _word);
		void commitHostWrite(bool _ramA, uint8_t _address);
		OutputFrame runProgram(const Voices& _voices);
		uint32_t hostPayload() const;

		// Frame-boundary captures must survive intervening host writes.
		std::array<uint32_t, 2> m_pendingVoiceSums{};
		std::array<uint32_t, 2> m_previousChorus{};
		uint32_t m_previousMixBase = 0;
		uint32_t m_previousMixSample = 0;
		uint8_t m_previousRamAddress = 0;
		uint16_t m_previousDelayHigh = 0;
		uint16_t m_previousDelayRead = 0;
		uint16_t m_delayCounter = 0xbbbb;
		bool m_hasPreviousFrame = false;
		// 256 KB, so it lives on the heap. Inline, it made the chip - and every board holding one - too
		// big for a 1 MB stack.
		std::vector<uint32_t> m_dram = std::vector<uint32_t>(DramSize, 0);
		uint16_t m_dramWordMask = 0x3fff;
		// C[2:0]/C[5:3] select serial timing taps; they do not affect parallel output.
		// D0: short program/bus mode (short execution unsupported); D1: ROM readback (unsupported).
		// D2: input sign convention; D6: serializer clear, treated as output mute.
		// D3: outward pin mux; D4: framing-latch set; D5: serial capture delay.
		std::array<uint8_t, 16> m_registers{};
		uint32_t m_readCapture = 0;
		std::array<uint32_t, RamASize> m_ramA{};
		Gains m_gains{};
		Routing m_routing{};
		// Retain unused parameter bits for host readback.
		std::array<uint32_t, RamBSize> m_hostParameters{};
		// Parameter bits 17:14, four nibbles per tap in descending significance.
		std::array<uint16_t, RamBSize / 4> m_delayTaps{};
	};
}
