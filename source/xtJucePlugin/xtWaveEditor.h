#pragma once

#include "weData.h"
#include "weGraphData.h"
#include "xtWaveEditorStyle.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "jucePluginLib/midipacket.h"

#include "juceUiLib/button.h"

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
		explicit WaveEditor(Editor& _editor, const juce::File& _cacheDir);
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

		void setSelectedTable(xt::TableId _index);
		void setSelectedWave(xt::WaveId _waveIndex, bool _forceRefresh = false);

	private:
		// ComponentMovementWatcher
		void componentVisibilityChanged() override { checkFirstTimeVisible(); }
		void componentPeerChanged() override { checkFirstTimeVisible(); }
		void componentMovedOrResized(bool wasMoved, bool wasResized) override { checkFirstTimeVisible(); }

		void checkFirstTimeVisible();
		void onFirstTimeVisible();

		void toggleWavePreview(bool _enabled);
		void toggleWavetablePreview(bool _enabled);

		void onWaveDataChanged(const xt::WaveData& _data) const;

		void saveWave();
		bool saveWaveTo(xt::WaveId _target);

		void saveWavetable();

		Editor& m_editor;

		std::unique_ptr<WaveTree> m_waveTree;
		std::unique_ptr<ControlTree> m_controlTree;
		std::unique_ptr<TablesTree> m_tablesTree;

		std::unique_ptr<GraphFreq> m_graphFreq;
		std::unique_ptr<GraphPhase> m_graphPhase;
		std::unique_ptr<GraphTime> m_graphTime;

		juce::Button* m_btWavePreview = nullptr;
		juce::Button* m_ledWavePreview = nullptr;

		juce::Button* m_btWavetablePreview = nullptr;
		juce::Button* m_ledWavetablePreview = nullptr;

		genericUI::Button<juce::DrawableButton>* m_btWaveSave = nullptr;
		juce::Button* m_btWavetableSave = nullptr;

		WaveEditorData m_data;
		GraphData m_graphData;

		bool m_wasVisible = false;

		xt::TableId m_selectedTable;
		xt::WaveId m_selectedWave;

		WaveEditorStyle m_style;
	};
}
