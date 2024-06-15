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

		void paint(juce::Graphics& _g, int _x, int _y, int _width, int _height) override;
	private:
	};
}
