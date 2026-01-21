#pragma once

#include "weData.h"
#include "weGraph.h"
#include "weGraphData.h"

#include "jucePluginLib/midipacket.h"

#include "juceRmlUi/rmlMenu.h"

namespace juce
{
	class FileChooser;
}

namespace Rml
{
	class Element;
}

namespace xtJucePlugin
{
	class GraphPhase;
	class GraphFreq;
	class GraphTime;
	class ControlTree;
	class TablesTree;
	class WaveTree;
	class Editor;

	class WaveEditor
	{
	public:
		explicit WaveEditor(Editor& _editor, Rml::Element* _parent, const juce::File& _cacheDir);
		WaveEditor() = delete;
		WaveEditor(const WaveEditor&) = delete;
		WaveEditor(WaveEditor&&) = delete;
		~WaveEditor();

		WaveEditor& operator = (const WaveEditor&) = delete;
		WaveEditor& operator = (WaveEditor&&) = delete;

		void initialize();
		void destroy();

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const synthLib::SysexBuffer& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const synthLib::SysexBuffer& _msg);

		const WaveEditorData& getData() const { return m_data; }
		WaveEditorData& getData() { return m_data; }

		Editor& getEditor() const { return m_editor; }
		GraphData& getGraphData() { return m_graphData; }

		void setSelectedTable(xt::TableId _index);
		void setSelectedWave(xt::WaveId _waveIndex, bool _forceRefresh = false);

		std::string getTableName(xt::TableId _id) const;

		xt::TableId getSelectedTable() const { return m_selectedTable; }

		juceRmlUi::Menu createCopyToSelectedTableMenu(xt::WaveId _id);
		static juceRmlUi::Menu createRamWavesPopupMenu(const std::function<void(xt::WaveId)>& _callback);

		void filesDropped(std::map<xt::WaveId, xt::WaveData>& _waves, std::map<xt::TableId, xt::TableData>& _tables, const std::vector<std::string>& _files);

		void openGraphPopupMenu(const Graph& _graph, Rml::Event& _event);

		void exportAsSyx(const xt::WaveId& _id, const xt::WaveData& _data);
		void exportAsMid(const xt::WaveId& _id, const xt::WaveData& _data);
		void exportAsSyxOrMid(const std::string& _filename, const xt::WaveId& _id, const xt::WaveData& _data, bool _midi) const;
		void exportAsSyxOrMid(const std::vector<xt::WaveId>& _ids, bool _midi);
		void exportAsWav(const xt::WaveData& _data);
		void exportAsWav(const std::string& _filename, const xt::WaveData& _data) const;

		void exportAsSyxOrMid(const xt::TableId& _table, bool _midi);
		void exportAsWav(const xt::TableId& _table);

		void exportAsWav(const std::string& _filename, const std::vector<int8_t>& _data) const;

		void exportToFile(const std::string& _filename, const synthLib::SysexBufferList& _sysex, bool _midi) const;
		void exportToFile(const std::string& _filename, const synthLib::SysexBuffer& _sysex, bool _midi) const
		{
			return exportToFile(_filename, synthLib::SysexBufferList{ _sysex }, _midi);
		}

		void selectImportFile(const std::function<void(const juce::String&)>& _callback);
		void selectExportFileName(const std::string& _title, const std::string& _extension, const std::function<void(const std::string&)>&);

		std::optional<xt::WaveData> importWaveFile(const std::string& _filename) const;

	private:
		void checkFirstTimeVisible();
		void onFirstTimeVisible();

		void toggleWavePreview(bool _enabled);
		void toggleWavetablePreview(bool _enabled);

		void onWaveDataChanged(const xt::WaveData& _data) const;

		bool saveWaveTo(xt::WaveId _target);

		Editor& m_editor;
		Rml::Element* m_parent = nullptr;

		WaveTree* m_waveTree;
		ControlTree* m_controlTree;
		TablesTree* m_tablesTree;

		std::unique_ptr<GraphFreq> m_graphFreq;
		std::unique_ptr<GraphPhase> m_graphPhase;
		std::unique_ptr<GraphTime> m_graphTime;

		WaveEditorData m_data;
		GraphData m_graphData;

		bool m_wasVisible = false;

		xt::TableId m_selectedTable;
		xt::WaveId m_selectedWave;

		std::unique_ptr<juce::FileChooser> m_fileChooser;

		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onUpdate;
	};
}
