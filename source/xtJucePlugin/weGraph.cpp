#include "weGraph.h"

#include "xtWaveEditor.h"
#include "dsp56kEmu/fastmath.h"

namespace xtJucePlugin
{
	Graph::Graph(WaveEditor& _editor) : m_editor(_editor), m_data(_editor.getGraphData())
	{
		m_onDataChanged.set(m_data.onChanged, [this]()
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
		const auto& style = m_editor.getStyle();

		const float scaleX = static_cast<float>(_width)  / static_cast<float>(_size);
		const float scaleY = static_cast<float>(_height);

		const float x = static_cast<float>(_x) + scaleX * 0.5f;
		const float y = static_cast<float>(_y);

		const float h = static_cast<float>(_height);

		for(uint32_t i=0; i<_size; ++i)
		{
			const auto x0 = static_cast<float>(i ? (i - 1) : i) * scaleX + x;
			const auto x1 = static_cast<float>(i    ) * scaleX + x;

			const auto y0 = h - (normalize(_data[i ? (i - 1) : i])) * scaleY + y - 1;
			const auto y1 = h - (normalize(_data[i    ])) * scaleY + y - 1;

			if(i)
			{
				_g.setColour(juce::Colour(style.colGraphLine));
				_g.drawLine(x0, y0, x1, y1, style.graphLineThickness);
			}

			if(m_highlightedIndices.find(i) != m_highlightedIndices.end())
			{
				_g.setColour(style.colGraphLineHighlighted);
				const auto s = style.graphPointSizeHighlighted;
				_g.fillEllipse(x1 - s * 0.5f, y1 - s * 0.5f, s, s);
			}
			else
			{
				_g.setColour(style.colGraphLine);
				const auto s = style.graphPointSize;
				_g.fillEllipse(x1 - s * 0.5f, y1 - s * 0.5f, s, s);
			}
		}
	}

	bool Graph::updateHoveredIndex(const juce::MouseEvent& _e)
	{
		const auto index = mouseToIndex(_e);
		if(!isValidIndex(index))
		{
			setHoveredIndex(InvalidIndex);
			return false;
		}
		setHoveredIndex(static_cast<uint32_t>(index));
		return true;
	}

	void Graph::mouseDown(const juce::MouseEvent& _e)
	{
		Component::mouseDown(_e);
		setLastMouseEvent(_e);
		const auto i = mouseToIndex(_e);
		if(isValidIndex(i))
			modifyValue(static_cast<uint32_t>(i), mouseToUnnormalizedValue(_e));
	}

	void Graph::mouseMove(const juce::MouseEvent& _e)
	{
		Component::mouseMove(_e);
		updateHoveredIndex(_e);
	}

	void Graph::mouseEnter(const juce::MouseEvent& _e)
	{
		Component::mouseEnter(_e);
		updateHoveredIndex(_e);
	}

	void Graph::mouseExit(const juce::MouseEvent& _e)
	{
		Component::mouseExit(_e);
		setHoveredIndex(InvalidIndex);
	}

	void Graph::mouseDrag(const juce::MouseEvent& _e)
	{
		Component::mouseDrag(_e);

		if(!_e.mods.isShiftDown())
		{
			updateHoveredIndex(_e);

			if(m_lastMouseEvent)
			{
				modifyValuesForRange(*m_lastMouseEvent, _e);
			}
			else if(isValidIndex(m_hoveredIndex))
			{
				modifyValue(m_hoveredIndex, mouseToUnnormalizedValue(_e));
			}
		}
		else
		{
			if(isValidIndex(m_hoveredIndex))
				modifyValue(m_hoveredIndex, mouseToUnnormalizedValue(_e));
		}

		setLastMouseEvent(_e);
	}

	int32_t Graph::mouseToIndex(const juce::MouseEvent& _e) const
	{
		return _e.x * static_cast<int32_t>(getDataSize()) / (int32_t)getWidth();
	}

	float Graph::mouseToNormalizedValue(const juce::MouseEvent& _e) const
	{
		const auto v = 1.0f - static_cast<float>(_e.y) / static_cast<float>(getHeight() - 1);
		return dsp56k::clamp(v, 0.0f, 1.0f);
	}

	float Graph::mouseToUnnormalizedValue(const juce::MouseEvent& _e) const
	{
		return unnormalize(mouseToNormalizedValue(_e));
	}

	const juce::MouseEvent* Graph::lastMouseEvent() const
	{
		return m_lastMouseEvent.get();
	}

	void Graph::onSourceChanged()
	{
		repaint();
	}

	void Graph::onHoveredIndexChanged(const uint32_t _index)
	{
		if(_index == InvalidIndex)
		{
			if(!m_highlightedIndices.empty())
				m_highlightedIndices.clear();

			onHighlightedIndicesChanged(m_highlightedIndices);
		}

		setHighlightedIndices({_index});
	}

	void Graph::onHighlightedIndicesChanged(const std::set<uint32_t>& _indices)
	{
		repaint();
	}

	void Graph::setHighlightedIndices(const std::set<uint32_t>& _indices)
	{
		bool changed = false;

		for (auto existing : m_highlightedIndices)
		{
			if(_indices.find(existing) == _indices.end())
			{
				changed = true;
				break;
			}
		}

		if(!changed)
		{
			for (uint32_t newIndex : _indices)
			{
				if(m_highlightedIndices.find(newIndex) == m_highlightedIndices.end())
				{
					changed = true;
					break;
				}
			}
		}

		if(!changed)
			return;

		m_highlightedIndices = _indices;

		onHighlightedIndicesChanged(m_highlightedIndices);
	}

	void Graph::modifyValue(uint32_t _index, float _unnormalizedValue)
	{
	}

	void Graph::setHoveredIndex(uint32_t _index)
	{
		if(_index == m_hoveredIndex)
			return;
		m_hoveredIndex = _index;
		onHoveredIndexChanged(m_hoveredIndex);
	}

	void Graph::setLastMouseEvent(const juce::MouseEvent& _e)
	{
		m_lastMouseEvent = std::make_unique<juce::MouseEvent>(_e);
	}

	void Graph::modifyValuesForRange(const juce::MouseEvent& _a, const juce::MouseEvent& _b)
	{
		auto indexA = mouseToIndex(_a);
		auto indexB = mouseToIndex(_b);

		auto valueA = mouseToUnnormalizedValue(_a);
		auto valueB = mouseToUnnormalizedValue(_b);

		if(indexA > indexB)
		{
			std::swap(indexA, indexB);
			std::swap(valueA, valueB);
		}

		const auto count = indexB - indexA + 1;

		const auto valueDiff = valueB - valueA;
		const auto factor = valueDiff / static_cast<float>(count);

		auto v = valueA;

		for(auto i = indexA; i <= indexB; ++i)
		{
			if(i < 0)
				continue;

			if(i >= static_cast<int32_t>(getDataSize()))
				return;

			modifyValue(i, v);
			v += factor;
		}
	}
}
