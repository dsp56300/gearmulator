#pragma once

namespace pluginLib
{
	class Processor;
	class MidiPorts;
}

namespace genericUI
{
	class Editor;
}

namespace juce
{
	class PopupMenu;
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

		static void createMidiInputMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts&);
		static void createMidiOutputMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts&);
		static void createMidiPortsMenu(juce::PopupMenu& _menu, pluginLib::MidiPorts&);

	private:
		pluginLib::MidiPorts& getMidiPorts() const;

		void showMidiPortFailedMessage(const char* _name) const;
		static void showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name);

		void updateMidiInput(int _index) const;
		void updateMidiOutput(int _index) const;

		Processor& m_processor;

		juce::ComboBox* m_midiIn = nullptr;
		juce::ComboBox* m_midiOut = nullptr;
	};
}
