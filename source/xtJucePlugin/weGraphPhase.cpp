#include "weGraphPhase.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	GraphPhase::GraphPhase(WaveEditor& _editor): Graph(_editor)
	{
	}

	float GraphPhase::normalize(const float _in) const
	{
		return (_in + 1.0f) * 0.5f;
	}

	float GraphPhase::unnormalize(const float _in) const
	{
		return _in * 2.0f - 1.0f;
	}

	const float* GraphPhase::getData() const
	{
		return getGraphData().getPhases().data();
	}

	size_t GraphPhase::getDataSize() const
	{
		return getGraphData().getPhases().size();
	}

	void GraphPhase::modifyValue(uint32_t _index, float _unnormalizedValue)
	{
		getGraphData().setPhase(_index, _unnormalizedValue);
	}
}
