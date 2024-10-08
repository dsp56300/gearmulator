#pragma once

#include "dspSingle.h"

namespace virusLib
{
	class DspSingleSnow final : public DspSingle
	{
	public:
		DspSingleSnow();

		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency) override;
		void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency) override;
	};
}
