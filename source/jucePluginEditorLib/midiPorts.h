#pragma once

namespace pluginLib
{
	class MidiPorts;
}

namespace genericUI
{
	class Editor;
}

namespace juce
{
	struct MidiDeviceInfo;
	class String;
	class AudioDeviceManager;
	class ComboBox;
}

namespace jucePluginEditorLib
{
	class Processor;

	class MidiPorts
	{
	public:
		explicit MidiPorts(const genericUI::Editor& _editor, Processor& _processor);

	private:
		pluginLib::MidiPorts& getMidiPorts() const;

		void showMidiPortFailedMessage(const char* _name) const;
		void updateMidiInput(int _index) const;
		void updateMidiOutput(int _index) const;

		Processor& m_processor;

		juce::ComboBox* m_midiIn = nullptr;
		juce::ComboBox* m_midiOut = nullptr;
	};
}
