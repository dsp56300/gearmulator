#pragma once

#include "weData.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "../jucePluginLib/midipacket.h"

namespace xtJucePlugin
{
	class Editor;

	class WaveEditor : public juce::Component, juce::ComponentMovementWatcher
	{
	public:
		explicit WaveEditor(juce::Component* _parent, Editor& _editor);
		~WaveEditor();

		void visibilityChanged() override;
		void parentHierarchyChanged() override;

		void onReceiveWave(const pluginLib::MidiPacket::Data& _data, const std::vector<uint8_t>& _msg);

	private:
		// ComponentMovementWatcher
		void componentVisibilityChanged() override;
		void componentPeerChanged() override {}
		void componentMovedOrResized(bool wasMoved, bool wasResized) override {}

		void checkFirstTimeVisible();
		void onFirstTimeVisible();

		Editor& m_editor;
		WaveEditorData m_data;
		bool m_wasVisible = false;
	};
}
