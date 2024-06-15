#pragma once

#include "weData.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../jucePluginLib/midipacket.h"

namespace xtJucePlugin
{
	class WaveTree;
	class Editor;

	class WaveEditor : public juce::Component, juce::ComponentMovementWatcher
	{
	public:
		explicit WaveEditor(Editor& _editor);
		~WaveEditor() override;

		void initialize();

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

		const WaveEditorData& getData() const { return m_data; }
		WaveEditorData& getData() { return m_data; }

		Editor& getEditor() const { return m_editor; }

	private:
		// ComponentMovementWatcher
		void componentVisibilityChanged() override { checkFirstTimeVisible(); }
		void componentPeerChanged() override { checkFirstTimeVisible(); }
		void componentMovedOrResized(bool wasMoved, bool wasResized) override { checkFirstTimeVisible(); }

		void checkFirstTimeVisible();
		void onFirstTimeVisible();

		Editor& m_editor;
		std::unique_ptr<WaveTree> m_waveTree;
		WaveEditorData m_data;
		bool m_wasVisible = false;
	};
}
