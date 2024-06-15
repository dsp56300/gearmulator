#include "weGraph.h"

#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	Graph::Graph(WaveEditor& _editor) : m_editor(_editor), m_data(_editor.getGraphData())
	{
		m_onSourceChanged.set(m_data.onSourceChanged, [this](const WaveData& _source)
		{
			onSourceChanged();
		});
	}

	void Graph::paint(juce::Graphics& g)
	{
		g.fillAll(findColour(juce::TreeView::ColourIds::backgroundColourId));

		paint(g, 0, 0, getWidth(), getHeight());
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
		const float scaleY = static_cast<float>(_height) / (m_maxValue - m_minValue);

		const float h = static_cast<float>(_height);

		for(uint32_t x=1; x<_size; ++x)
		{
			const auto x0 = static_cast<float>(x - 1) * scaleX + static_cast<float>(_x);
			const auto x1 = static_cast<float>(x    ) * scaleX + static_cast<float>(_x);

			const auto y0 = h - (_data[x - 1] - m_minValue) * scaleY + static_cast<float>(_y) - 1;
			const auto y1 = h - (_data[x    ] - m_minValue) * scaleY + static_cast<float>(_y) - 1;

			_g.drawLine(x0, y0, x1, y1, 3.0f);
		}
	}

	void Graph::setRange(const float _min, const float _max)
	{
		m_minValue = _min;
		m_maxValue = _max;
	}

	void Graph::onSourceChanged()
	{
		repaint();
	}
}
