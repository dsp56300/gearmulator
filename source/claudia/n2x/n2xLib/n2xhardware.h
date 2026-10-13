#pragma once

#include "n2xdsp.h"
#include "n2xmc.h"
#include "n2xrom.h"

#include "synthLib/audioTypes.h"
#include "synthLib/midiInputQueue.h"
#include "synthLib/midiTypes.h"

#include "dsp56kBase/sharedaudioreducer.h"

namespace n2x
{
	class Hardware
	{
	public:
		using AudioOutputs = std::array<std::vector<dsp56k::TWord>, 4>;
		Hardware(const std::vector<uint8_t>& _romData = {}, const std::string& _romName = {});
		~Hardware();

		bool isValid() const;

		void processUC();

		Microcontroller& getUC() {return m_uc; }

		const auto& getAudioOutputs() const { return m_audioOutputs; }

		void processAudio(uint32_t _frames, uint32_t _latency);

		auto& getDSPA() { return m_dspA; }
		auto& getDSPB() { return m_dspB; }

		auto& getMidi() { return m_uc.getMidi(); }

		void haltDSPs();
		void resumeDSPs();
		bool requestingHaltDSPs() const { return m_dspHalted; }

		bool getButtonState(ButtonType _type) const;
		void setButtonState(ButtonType _type, bool _pressed);

		// Presses SHIFT, then _button while SHIFT is held, the way the hardware is played. Panic is SHIFT + FilterDist
		void pressShiftCombination(ButtonType _button);

		uint8_t getKnobPosition(KnobType _knob) const;
		void setKnobPosition(KnobType _knob, uint8_t _value);

		void processAudio(const synthLib::TAudioOutputs& _outputs, uint32_t _frames, uint32_t _latency);
		bool sendMidi(const synthLib::SMidiEvent& _ev);
		void notifyBootFinished();

		const std::string& getRomFilename() const { return m_rom.getFilename(); }

	private:
		// The four DAC words of a frame, sign extended
		using MixFrame = std::array<int32_t, 4>;

		struct MixReduce
		{
			void operator()(MixFrame& _dst, const MixFrame& _src) const
			{
				for(size_t i=0; i<_dst.size(); ++i)
					_dst[i] += _src[i];
			}
		};

		void ensureBufferSize(uint32_t _frames);
		void initMixer();
		void onMixedFrame(const MixFrame& _frame);
		void processMidiInput();
		void onEsaiCallbackB();
		void syncUCtoDSP();
		void ucThreadFunc();
		void advanceSamples(uint32_t _samples, uint32_t _latency);
		void processShiftCombination(uint32_t _frames);

		Rom m_rom;
		Microcontroller m_uc;
		DSP m_dspA;
		DSP m_dspB;

		// On the hardware, DSP B receives the four slots of DSP A and passes them unchanged to its outputs. Here both
		// DSPs run side by side, DSP B gets silence, and their frames are added and saturated as DSP B does it. Each
		// DSP may run at most MixerCapacity frames ahead of the other (EMU-61)
		static constexpr uint32_t MixerCapacity = 16;
		dsp56k::SharedAudioReducer<MixFrame, MixerCapacity, 2, MixReduce> m_mixer;
		uint32_t m_mixerLaneA = 0;
		uint32_t m_mixerLaneB = 0;
		dsp56k::RingBuffer<std::array<dsp56k::TWord, 4>, dsp56k::Audio::RingBufferSize, true, false> m_mixedOutput;

		AudioOutputs m_audioOutputs;

		// timing
		const double m_samplerateInv;
		uint32_t m_esaiFrameIndex = 0;
		uint32_t m_lastEsaiFrameIndex = 0;
		int64_t m_remainingUcCycles = 0;
		double m_remainingUcCyclesD = 0;
		std::mutex m_esaiFrameAddedMutex;
		dsp56k::ConditionVariable m_esaiFrameAddedCv;
		std::mutex m_requestedFramesAvailableMutex;
		dsp56k::ConditionVariable m_requestedFramesAvailableCv;
		size_t m_requestedFrames = 0;
		bool m_dspHalted = false;

		std::unique_ptr<std::thread> m_ucThread;
		bool m_destroy = false;

		// Midi
		synthLib::MidiInputQueue m_midiIn;
		uint32_t m_midiOffsetCounter = 0;

		// DSP slowdown
		uint32_t m_esaiLatency = 0;
		int32_t m_dspNotifyCorrection = 0;
		dsp56k::SpscSemaphoreWithCount m_haltDSPSem;

		bool m_bootFinished = false;

		// SHIFT combination: 0 = idle, then SHIFT is down, the button is down, the button is up again
		uint32_t m_shiftCombinationStep = 0;
		uint32_t m_shiftCombinationWait = 0;
		ButtonType m_shiftCombinationButton = ButtonType::Shift;
	};
}
