#include "weGraphTime.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	GraphTime::GraphTime(WaveEditor& _editor): Graph(_editor)
	{
		setRange(-1.0f, 1.0f);
	}

	void GraphTime::paint(juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height)
	{
		Graph::paint(getGraphData().getData(), _g, _x, _y, _width, _height);
	}
}
