#include "weGraphPhase.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	GraphPhase::GraphPhase(WaveEditor& _editor): Graph(_editor)
	{
		setRange(-1,1);
	}

	void GraphPhase::paint(juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height)
	{
		Graph::paint(getGraphData().getPhases(), _g, _x, _y, _width, _height);
	}
}
