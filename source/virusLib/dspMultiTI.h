#pragma once

#include "buildconfig.h"

#if VIRUS_SUPPORT_TI

#include "dspSingle.h"

#include "../synthLib/audiobuffer.h"

namespace virusLib
{
	class DspMultiTI final : public DspSingle
	{
	public:
		template<typename T, size_t Size> using EsaiBuf = std::array<std::vector<T>, Size>;

		template<typename T> struct EsaiBufs
		{
			EsaiBuf<T, 2> input;
			EsaiBuf<T, 4> dspAout;
			EsaiBuf<T, 4> dspBout;
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

#endif
