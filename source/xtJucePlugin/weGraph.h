#pragma once

#include "weTypes.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "jucePluginLib/event.h"

namespace xtJucePlugin
{
	class GraphData;
	class WaveEditor;

	class Graph : public juce::Component
	{
	public:
		static constexpr uint32_t InvalidIndex = ~0;

		explicit Graph(WaveEditor& _editor);

		WaveEditor& getEditor() const { return m_editor; }

		void paint(juce::Graphics& g) override;
		void parentHierarchyChanged() override;

		template<size_t Size> void paint(const std::array<float, Size>& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height) const
		{
			paint(_data.data(), Size, _g, _x, _y, _width, _height);
		}

		void paint(const float* _data, size_t _size, juce::Graphics& _g, int _x, int _y, int _width, int _height) const;

		virtual float normalize(float _in) const = 0;
		virtual float unnormalize(float _in) const = 0;

		virtual const float* getData() const = 0;
		virtual size_t getDataSize() const = 0;

		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseMove(const juce::MouseEvent& _e) override;
		void mouseEnter(const juce::MouseEvent& _e) override;
		void mouseExit(const juce::MouseEvent& _e) override;
		void mouseDrag(const juce::MouseEvent& _e) override;

		GraphData& getGraphData() const { return m_data; }

		int32_t mouseToIndex(const juce::MouseEvent& _e) const;
		float mouseToNormalizedValue(const juce::MouseEvent& _e) const;
		float mouseToUnnormalizedValue(const juce::MouseEvent& _e) const;

		bool isValidIndex(const uint32_t _index) const { return _index < getDataSize(); }
		bool isValidIndex(const int32_t _index) const { return _index >= 0 && _index < getDataSize(); }

		const juce::MouseEvent* lastMouseEvent() const;

		void setUpdateHoveredPositionWhileDragging(bool _enable)
		{
			m_updateHoveredPositionWhileDragging = _enable;
		}

	protected:
		virtual void onSourceChanged();
		virtual void onHoveredIndexChanged(uint32_t _index);
		virtual void onHighlightedIndicesChanged(const std::set<uint32_t>& _indices);

		void setHighlightedIndices(const std::set<uint32_t>& _indices);

		virtual void modifyValue(uint32_t _index, float _unnormalizedValue) = 0;

		bool updateHoveredIndex(const juce::MouseEvent& _e);

		uint32_t getHoveredIndex() const { return m_hoveredIndex; }

	private:
		void setHoveredIndex(uint32_t _index);
		void setLastMouseEvent(const juce::MouseEvent& _e);
		void modifyValuesForRange(const juce::MouseEvent& _a, const juce::MouseEvent& _b);

		WaveEditor& m_editor;
		GraphData& m_data;

		pluginLib::EventListener<> m_onDataChanged;

		std::set<uint32_t> m_highlightedIndices;
		uint32_t m_hoveredIndex = InvalidIndex;
		std::unique_ptr<juce::MouseEvent> m_lastMouseEvent;
		bool m_updateHoveredPositionWhileDragging = false;
	};
}
