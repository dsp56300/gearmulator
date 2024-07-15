#pragma once

#include "weData.h"
#include "weGraphData.h"
#include "xtWaveEditorStyle.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "jucePluginLib/midipacket.h"

namespace xtJucePlugin
{
	class GraphPhase;
	class GraphFreq;
	class GraphTime;
	class ControlTree;
	class TablesTree;
	class WaveTree;
	class Editor;

	class WaveEditor : public juce::Component, juce::ComponentMovementWatcher
	{
	public:
		explicit WaveEditor(Editor& _editor);
		WaveEditor() = delete;
		WaveEditor(const WaveEditor&) = delete;
		WaveEditor(WaveEditor&&) = delete;
		~WaveEditor() override;

		WaveEditor& operator = (const WaveEditor&) = delete;
		WaveEditor& operator = (WaveEditor&&) = delete;

		void initialize();
		void destroy();

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

		const WaveEditorData& getData() const { return m_data; }
		WaveEditorData& getData() { return m_data; }

		const WaveEditorStyle& getStyle() const { return m_style; }

		Editor& getEditor() const { return m_editor; }
		GraphData& getGraphData() { return m_graphData; }

		void setSelectedTable(uint32_t _index);
		void setSelectedWave(uint32_t _waveIndex, bool _forceRefresh = false);

	private:
		// ComponentMovementWatcher
		void componentVisibilityChanged() override { checkFirstTimeVisible(); }
		void componentPeerChanged() override { checkFirstTimeVisible(); }
		void componentMovedOrResized(bool wasMoved, bool wasResized) override { checkFirstTimeVisible(); }

		void checkFirstTimeVisible();
		void onFirstTimeVisible();

		Editor& m_editor;

		std::unique_ptr<WaveTree> m_waveTree;
		std::unique_ptr<ControlTree> m_controlTree;
		std::unique_ptr<TablesTree> m_tablesTree;

		std::unique_ptr<GraphFreq> m_graphFreq;
		std::unique_ptr<GraphPhase> m_graphPhase;
		std::unique_ptr<GraphTime> m_graphTime;

		WaveEditorData m_data;
		GraphData m_graphData;

		bool m_wasVisible = false;

		uint32_t m_selectedTable = ~0;
		uint32_t m_selectedWave = ~0;

		WaveEditorStyle m_style;
	};
}
