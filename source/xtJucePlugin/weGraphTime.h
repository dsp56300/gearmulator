#pragma once

#include "weGraph.h"

namespace xtJucePlugin
{
	class GraphTime : public Graph
	{
	public:
		explicit GraphTime(WaveEditor& _editor);

		void paint(juce::Graphics& _g, int _x, int _y, int _width, int _height) override;
	private:
	};
}
