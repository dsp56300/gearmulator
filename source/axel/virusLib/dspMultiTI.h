#pragma once

#include "dspSingle.h"

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

		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency) override;
		void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency) override;

		DspSingle& getDSP2() { return m_dsp2; }

	private:
		DspSingle m_dsp2;

		EsaiBufs<float> m_bufferF;
		EsaiBufs<dsp56k::TWord> m_bufferI;
	};
}
