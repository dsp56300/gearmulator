#include "weGraphTime.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	GraphTime::GraphTime(WaveEditor& _editor, Rml::Element* _parent) : Graph(_editor, _parent)
	{
		setUpdateHoveredPositionWhileDragging(true);
	}

	float GraphTime::normalize(const float _in) const
	{
		return (_in + 1.0f) * 0.5f;
	}

	float GraphTime::unnormalize(const float _in) const
	{
		return _in * 2.0f - 1.0f;
	}

	const float* GraphTime::getData() const
	{
		return getGraphData().getData().data();
	}

	size_t GraphTime::getDataSize() const
	{
		return getGraphData().getData().size();
	}

	void GraphTime::modifyValue(const uint32_t _index, const float _unnormalizedValue)
	{
		getGraphData().setData(_index, _unnormalizedValue);
	}
}
