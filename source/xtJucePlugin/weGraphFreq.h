#pragma once

#include "weGraph.h"

namespace xtJucePlugin
{
	class GraphFreq : public Graph
	{
	public:
		GraphFreq(WaveEditor& _editor) : Graph(_editor)
		{
		}

		float normalize(float _in) const override;
		float unnormalize(float _in) const override;

		const float* getData() const override;
		size_t getDataSize() const override;
		void modifyValue(uint32_t _index, float _unnormalizedValue) override;
	private:
	};
}
