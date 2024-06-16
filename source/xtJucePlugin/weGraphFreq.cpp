#include "weGraphFreq.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	float GraphFreq::normalize(const float _in) const
	{
		return _in;
	}

	float GraphFreq::unnormalize(const float _in) const
	{
		return _in;
	}

	const float* GraphFreq::getData() const
	{
		return getGraphData().getFrequencies().data();
	}

	size_t GraphFreq::getDataSize() const
	{
		return getGraphData().getFrequencies().size();
	}
}
