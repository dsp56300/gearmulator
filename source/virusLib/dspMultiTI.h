#pragma once

#include "dspSingle.h"

#include "../synthLib/audiobuffer.h"

namespace virusLib
{
	class DspMultiTI final : public DspSingle
	{
	public:
		DspMultiTI();

		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency) override;
		void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency) override;

		DspSingle& getDSP2() { return m_dsp2; }

	private:
		DspSingle m_dsp2;
		std::vector<std::vector<float>> m_bufferF;
		std::vector<std::vector<dsp56k::TWord>> m_bufferI;
	};
}
