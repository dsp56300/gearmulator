#include "weGraph.h"

#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	Graph::Graph(WaveEditor& _editor) : m_editor(_editor), m_data(_editor.getGraphData())
	{
		m_onSourceChanged.set(m_data.onSourceChanged, [this](const WaveData&)
		{
			onSourceChanged();
		});
	}

	void Graph::paint(juce::Graphics& g)
	{
		g.fillAll(findColour(juce::TreeView::ColourIds::backgroundColourId));

		paint(getData(), getDataSize(),g, 0, 0, getWidth(), getHeight());
	}

	void Graph::parentHierarchyChanged()
	{
		juce::Component::parentHierarchyChanged();

		const auto* parent = getParentComponent();
		if(!parent)
			return;

		setSize(parent->getWidth(), parent->getHeight());
	}

	void Graph::paint(const float* _data, const size_t _size, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height) const
	{
		_g.setColour(juce::Colour(0xffffffff));

		const float scaleX = static_cast<float>(_width - 1)  / static_cast<float>(_size -1);
		const float scaleY = static_cast<float>(_height);

		const float x = static_cast<float>(_x);
		const float y = static_cast<float>(_y);

		const float h = static_cast<float>(_height);

		for(uint32_t i=0; i<_size; ++i)
		{
			const auto x0 = static_cast<float>(i ? (i - 1) : i) * scaleX + x;
			const auto x1 = static_cast<float>(i    ) * scaleX + x;

			const auto y0 = h - (normalize(_data[i ? (i - 1) : i])) * scaleY + y - 1;
			const auto y1 = h - (normalize(_data[i    ])) * scaleY + y - 1;

			if(i)
				_g.drawLine(x0, y0, x1, y1, 3.0f);

			_g.fillEllipse(x0 - 5, y0 - 5, 10, 10);
		}
	}

	void Graph::onSourceChanged()
	{
		repaint();
	}
}
