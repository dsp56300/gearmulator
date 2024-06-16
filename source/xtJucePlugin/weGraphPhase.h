#pragma once

#include "weGraph.h"

namespace xtJucePlugin
{
	class GraphPhase : public Graph
	{
	public:
		explicit GraphPhase(WaveEditor& _editor);

		float normalize(float _in) const override;
		float unnormalize(float _in) const override;
		const float* getData() const override;
		size_t getDataSize() const override;
	private:
	};
}
