#include "weGraphFreq.h"

#include "weGraphData.h"

namespace xtJucePlugin
{
	void GraphFreq::paint(juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height)
	{
		Graph::paint(getGraphData().getFrequencies(), _g, _x, _y, _width, _height);
	}
}
