#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>

#include "dspSingle.h"

#include "dsp56kBase/ringbuffer.h"

#include "synthLib/audiobuffer.h"

namespace virusLib
{
	class DspMultiTI final : public DspSingle
	{
	public:
		static constexpr uint32_t InvalidOffset		= 0xffffffff;

		class Esai1Out : public std::vector<dsp56k::TWord>
		{
		public:
			template<typename T>
			void processAudioOutput(dsp56k::Esai& _esai, uint32_t _frames, const synthLib::TAudioOutputsT<T>& _outputs, uint32_t _firstOutChannel, const std::array<uint32_t, 6>& _sourceIndices);

		private:
			uint32_t m_blockStart = InvalidOffset;
		};

		class Esai1in : public std::vector<dsp56k::TWord>
		{
		public:
			template<typename T>
			void processAudioinput(dsp56k::Esai& _esai, uint32_t _frames, uint32_t _latency, const synthLib::TAudioInputsT<T>& _inputs);

		private:
			uint32_t m_magicTimer = 0;
			dsp56k::TWord m_previousInput = 0;
		};

		template<typename T> struct EsaiBufs
		{
			Esai1Out dspA;
			Esai1Out dspB;
			Esai1in in;
			T m_previousInput = 0;
			std::vector<T> dummyInput;
			std::vector<T> dummyOutput;
		};

		DspMultiTI();
		~DspMultiTI() override;

		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency) override;
		void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency) override;

		DspSingle& getDSP2() { return m_dsp2; }

	private:
		// The OS of each DSP talks to the other one in the second slot of the ESAI_1 frames: a marker once per ring of
		// 256 samples, then per block its clock, load and parts, and commands such as a note it hands over because it
		// is busy. The DSPs only take part when they find the marker of the other one where it belongs (EMU-240)
		class ControlLink
		{
		public:
			using Words = std::array<dsp56k::TWord, 2>;

			void prefill(uint32_t _count);
			void push(const Words& _words);
			Words pop();
			bool empty() const { return m_words.empty(); }

			// the DSP that writes into this link is about to wait, for audio or for the other DSP
			void producerWaits();

			void close();

		private:
			void wakeReceiver(size_t _minWords);

			dsp56k::RingBuffer<Words, 8192, false> m_words;
			std::mutex m_mutex;
			std::condition_variable m_cv;
			std::atomic<bool> m_receiverWaits{false};
			std::atomic<bool> m_producerWaits{false};
			bool m_closed = false;
			bool m_overflow = false;
		};

		static void connect(DspSingle& _dsp, ControlLink& _in, ControlLink& _out);

		DspSingle m_dsp2;

		ControlLink m_toSlave;
		ControlLink m_toMaster;

		EsaiBufs<float> m_bufferF;
		EsaiBufs<dsp56k::TWord> m_bufferI;
	};
}
