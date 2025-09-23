#include "weGraph.h"

#include "xtWaveEditor.h"
#include "baseLib/filesystem.h"

#include "dsp56kEmu/fastmath.h"

#include "juceRmlUi/rmlElemCanvas.h"
#include "juceRmlUi/rmlHelper.h"

namespace xtJucePlugin
{
	Graph::Graph(WaveEditor& _editor, Rml::Element* _parent) : m_editor(_editor), m_parent(_parent), m_data(_editor.getGraphData())
	{
		_parent->SetClass("x-we-graph", true);

		m_onDataChanged.set(m_data.onChanged, [this]
		{
			onSourceChanged();
		});

		m_canvas = juceRmlUi::ElemCanvas::create(_parent);

		m_canvas->setRepaintGraphicsCallback([this](const juce::Image& _image, juce::Graphics& _graphics)
		{
			m_width = _image.getWidth();
			m_height = _image.getHeight();
			paint(_graphics);
		});

		_parent->AddEventListener(Rml::EventId::Mousedown, this);
		_parent->AddEventListener(Rml::EventId::Mouseover, this);
		_parent->AddEventListener(Rml::EventId::Mouseout, this);
		_parent->AddEventListener(Rml::EventId::Mousemove, this);
		_parent->AddEventListener(Rml::EventId::Drag, this);

		_parent->SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::Drag);
	}
	Graph::~Graph()
	{
		m_parent->RemoveEventListener(Rml::EventId::Mousedown, this);
		m_parent->RemoveEventListener(Rml::EventId::Mouseover, this);
		m_parent->RemoveEventListener(Rml::EventId::Mouseout, this);
		m_parent->RemoveEventListener(Rml::EventId::Mousemove, this);
		m_parent->RemoveEventListener(Rml::EventId::Drag, this);
	}

	void Graph::paint(juce::Graphics& g)
	{
		g.fillAll(juce::Colours::black);

		paint(getData(), getDataSize(),g, 0, 0, getWidth(), getHeight());
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

	bool Graph::updateHoveredIndex(const Rml::Event& _e)
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

	void Graph::ProcessEvent(Rml::Event& event)
	{
		switch (event.GetId())
		{
		case Rml::EventId::Mousedown:	mouseDown(event); break;
		case Rml::EventId::Mousemove:	mouseMove(event); break;
		case Rml::EventId::Mouseover:	mouseEnter(event); break;
		case Rml::EventId::Mouseout:	mouseExit(event); break;
		case Rml::EventId::Drag:		mouseDrag(event); break;
		default:;
		}
	}

	void Graph::mouseDown(Rml::Event& _e)
	{
		if (juceRmlUi::helper::getMouseButton(_e) == juceRmlUi::MouseButton::Right)
		{
			m_editor.openGraphPopupMenu(*this, _e);
			return;
		}

		setLastMouseEvent(_e);
		const auto i = mouseToIndex(_e);
		if(isValidIndex(i))
			modifyValue(static_cast<uint32_t>(i), mouseToUnnormalizedValue(_e));
	}

	void Graph::mouseMove(Rml::Event& _e)
	{
		updateHoveredIndex(_e);
		setLastMouseEvent(_e);
	}

	void Graph::mouseEnter(Rml::Event& _e)
	{
		updateHoveredIndex(_e);
	}

	void Graph::mouseExit(Rml::Event& _e)
	{
		setHoveredIndex(InvalidIndex);
	}

	void Graph::mouseDrag(Rml::Event& _e)
	{
		if (juceRmlUi::helper::getMouseButton(_e) != juceRmlUi::MouseButton::Left)
			return;

		if(!juceRmlUi::helper::getKeyModShift(_e))
		{
			updateHoveredIndex(_e);

			if(m_lastMouseEvent)
			{
				modifyValuesForRange(*m_lastMouseEvent, getMousePos(_e));
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

	int32_t Graph::mouseToIndex(const Rml::Event& _e) const
	{
		return mouseToIndex(getMousePos(_e));
	}

	int32_t Graph::mouseToIndex(const Rml::Vector2f& _e) const
	{
		return static_cast<int32_t>(_e.x) * static_cast<int32_t>(getDataSize()) / (int32_t)getWidth();
	}

	float Graph::mouseToNormalizedValue(const Rml::Event& _e) const
	{
		const auto mousePos = getMousePos(_e);
		return mouseToNormalizedValue(mousePos);
	}

	float Graph::mouseToNormalizedValue(const Rml::Vector2f& _e) const
	{
		const auto v = 1.0f - static_cast<float>(_e.y) / static_cast<float>(getHeight() - 1);
		return dsp56k::clamp(v, 0.0f, 1.0f);
	}

	float Graph::mouseToUnnormalizedValue(const Rml::Event& _e) const
	{
		return unnormalize(mouseToNormalizedValue(_e));
	}

	float Graph::mouseToUnnormalizedValue(const Rml::Vector2f& _e) const
	{
		return unnormalize(mouseToNormalizedValue(_e));
	}

	Rml::Vector2f Graph::getMousePos(const Rml::Event& _e) const
	{
		auto p = juceRmlUi::helper::getMousePos(_e);
		p.x -= m_parent->GetAbsoluteLeft();
		p.y -= m_parent->GetAbsoluteTop();
		return p;
	}

	const Rml::Vector2f* Graph::lastMouseEvent() const
	{
		return m_lastMouseEvent.get();
	}

	bool Graph::canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files)
	{
		if (_files.size() != 1)
			return false;
		return baseLib::filesystem::hasExtension(_files.front(), ".wav");
	}

	void Graph::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		if (_files.size() != 1)
			return;
		if (auto res = m_editor.importWaveFile(_files[0]))
			m_editor.getGraphData().set(*res);
	}

	void Graph::onSourceChanged()
	{
		m_canvas->repaint();
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
		m_canvas->repaint();
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

	void Graph::setLastMouseEvent(const Rml::Event& _e)
	{
		m_lastMouseEvent = std::make_unique<Rml::Vector2f>(getMousePos(_e));
	}

	void Graph::modifyValuesForRange(const Rml::Vector2f& _a, const Rml::Vector2f& _b)
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
