#pragma once

#include <set>

#include "baseLib/event.h"

#include "juceRmlUi/rmlDragTarget.h"

namespace juceRmlUi
{
	class ElemCanvas;
}

namespace juce
{
	class Graphics;
}

namespace Rml
{
	class Event;
}

namespace xtJucePlugin
{
	class GraphData;
	class WaveEditor;

	class Graph : public juceRmlUi::DragTarget, Rml::EventListener
	{
	public:
		static constexpr uint32_t InvalidIndex = ~0;

		explicit Graph(WaveEditor& _editor, Rml::Element* _parent);

		~Graph();

		WaveEditor& getEditor() const { return m_editor; }

		void paint(juce::Graphics& g);

		template<size_t Size> void paint(const std::array<float, Size>& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height) const
		{
			paint(_data.data(), Size, _g, _x, _y, _width, _height);
		}

		void paint(const float* _data, size_t _size, juce::Graphics& _g, int _x, int _y, int _width, int _height) const;

		virtual float normalize(float _in) const = 0;
		virtual float unnormalize(float _in) const = 0;

		virtual const float* getData() const = 0;
		virtual size_t getDataSize() const = 0;

		void ProcessEvent(Rml::Event& event) override;

		virtual void mouseDown(Rml::Event& _e);
		virtual void mouseMove(Rml::Event& _e);
		virtual void mouseEnter(Rml::Event& _e);
		virtual void mouseExit(Rml::Event& _e);
		virtual void mouseDrag(Rml::Event& _e);

		GraphData& getGraphData() const { return m_data; }

		int32_t mouseToIndex(const Rml::Event& _e) const;
		int32_t mouseToIndex(const Rml::Vector2f& _e) const;
		float mouseToNormalizedValue(const Rml::Event& _e) const;
		float mouseToNormalizedValue(const Rml::Vector2f& _e) const;
		float mouseToUnnormalizedValue(const Rml::Event& _e) const;
		float mouseToUnnormalizedValue(const Rml::Vector2f& _e) const;

		bool isValidIndex(const uint32_t _index) const { return _index < getDataSize(); }
		bool isValidIndex(const int32_t _index) const { return _index >= 0 && _index < getDataSize(); }

		Rml::Vector2f getMousePos(const Rml::Event& _e) const;
		const Rml::Vector2f* lastMouseEvent() const;

		void setUpdateHoveredPositionWhileDragging(bool _enable)
		{
			m_updateHoveredPositionWhileDragging = _enable;
		}

		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) override;
		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;

	protected:
		virtual void onSourceChanged();
		virtual void onHoveredIndexChanged(uint32_t _index);
		virtual void onHighlightedIndicesChanged(const std::set<uint32_t>& _indices);

		void setHighlightedIndices(const std::set<uint32_t>& _indices);

		virtual void modifyValue(uint32_t _index, float _unnormalizedValue) = 0;

		bool updateHoveredIndex(const Rml::Event& _e);

		uint32_t getHoveredIndex() const { return m_hoveredIndex; }

		int getWidth() const { return m_width; }
		int getHeight() const { return m_height; }

	private:
		void setHoveredIndex(uint32_t _index);
		void setLastMouseEvent(const Rml::Event& _e);
		void modifyValuesForRange(const Rml::Vector2f& _a, const Rml::Vector2f& _b);

		WaveEditor& m_editor;
		Rml::Element* const m_parent;

		GraphData& m_data;

		baseLib::EventListener<> m_onDataChanged;

		std::set<uint32_t> m_highlightedIndices;
		uint32_t m_hoveredIndex = InvalidIndex;
		std::unique_ptr<Rml::Vector2f> m_lastMouseEvent;
		bool m_updateHoveredPositionWhileDragging = false;

		juceRmlUi::ElemCanvas* m_canvas;
		int m_width = 0;
		int m_height = 0;
	};
}
