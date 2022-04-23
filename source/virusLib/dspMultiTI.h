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

		DspSingle& getDSP2() { return m_dsp2; }

	private:
		DspSingle m_dsp2;
		synthLib::AudioBuffer m_buffer;
	};
}
