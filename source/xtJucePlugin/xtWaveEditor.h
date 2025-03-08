#pragma once

#include "weData.h"
#include "weGraph.h"
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

		std::string getTableName(xt::TableId _id) const;

		xt::TableId getSelectedTable() const { return m_selectedTable; }

		juce::PopupMenu createCopyToSelectedTableMenu(xt::WaveId _id);
		static juce::PopupMenu createRamWavesPopupMenu(const std::function<void(xt::WaveId)>& _callback);

		void filesDropped(std::map<xt::WaveId, xt::WaveData>& _waves, std::map<xt::TableId, xt::TableData>& _tables, const juce::StringArray& _files);

		void openGraphPopupMenu(const Graph& _graph, const juce::MouseEvent& _event);

		void exportAsSyx(const xt::WaveId& _id, const xt::WaveData& _data);
		void exportAsMid(const xt::WaveId& _id, const xt::WaveData& _data);
		void exportAsSyxOrMid(const std::string& _filename, const xt::WaveId& _id, const xt::WaveData& _data, bool _midi) const;
		void exportAsSyxOrMid(const std::vector<xt::WaveId>& _ids, bool _midi);
		void exportAsWav(const xt::WaveData& _data);
		void exportAsWav(const std::string& _filename, const xt::WaveData& _data) const;

		void selectImportFile(const std::function<void(const juce::String&)>& _callback);
		void selectExportFileName(const std::string& _title, const std::string& _extension, const std::function<void(const std::string&)>&);

		std::optional<xt::WaveData> importWaveFile(const std::string& _filename) const;

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

		bool saveWaveTo(xt::WaveId _target);

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

		xt::TableId m_selectedTable;
		xt::WaveId m_selectedWave;

		WaveEditorStyle m_style;

		std::unique_ptr<juce::FileChooser> m_fileChooser;
	};
}
