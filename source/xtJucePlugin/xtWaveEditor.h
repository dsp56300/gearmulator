#pragma once

#include "weData.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "../jucePluginLib/midipacket.h"

namespace xtJucePlugin
{
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

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);
		void onReceiveTable(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

		const WaveEditorData& getData() const { return m_data; }
		WaveEditorData& getData() { return m_data; }

		Editor& getEditor() const { return m_editor; }

		void setSelectedTable(uint32_t _index);

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
		WaveEditorData m_data;
		bool m_wasVisible = false;

		uint32_t m_selectedTable = ~0;
		uint32_t m_selectedWave = ~0;
	};
}
