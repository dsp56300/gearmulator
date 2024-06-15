#pragma once

#include "weTypes.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../jucePluginLib/event.h"

namespace xtJucePlugin
{
	class GraphData;
	class WaveEditor;

	class Graph : public juce::Component
	{
	public:
		explicit Graph(WaveEditor& _editor);

		WaveEditor& getEditor() const { return m_editor; }

		void paint(juce::Graphics& g) override;
		void parentHierarchyChanged() override;

		virtual void paint(juce::Graphics& _g, int _x, int _y, int _width, int _height) = 0;

		template<size_t Size> void paint(const std::array<float, Size>& _data, juce::Graphics& _g, const int _x, const int _y, const int _width, const int _height) const
		{
			paint(_data.data(), Size, _g, _x, _y, _width, _height);
		}

		void paint(const float* _data, size_t _size, juce::Graphics& _g, int _x, int _y, int _width, int _height) const;

		void setRange(float _min, float _max);

	protected:
		GraphData& getGraphData() const { return m_data; }
		virtual void onSourceChanged();

	private:
		WaveEditor& m_editor;
		GraphData& m_data;
		pluginLib::EventListener<WaveData> m_onSourceChanged;
		float m_minValue = 0.0f;
		float m_maxValue = 1.0f;
	};
}
