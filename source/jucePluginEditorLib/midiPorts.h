#pragma once

namespace juceRmlUi
{
	class ElemComboBox;
}

namespace juceRmlUi
{
	class Menu;
}

namespace pluginLib
{
	class Processor;
	class MidiPorts;
}

namespace juce
{
	struct MidiDeviceInfo;
	class String;
	class AudioDeviceManager;
}

namespace jucePluginEditorLib
{
	class Editor;
	class Processor;

	class MidiPorts
	{
	public:
		explicit MidiPorts(const Editor& _editor, Processor& _processor);

		static void createMidiInputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);
		static void createMidiOutputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);
		static void createMidiPortsMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);

	private:
		pluginLib::MidiPorts& getMidiPorts() const;

		void showMidiPortFailedMessage(const char* _name) const;
		static void showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name);

		void updateMidiInput(int _index) const;
		void updateMidiOutput(int _index) const;

		Processor& m_processor;

		juceRmlUi::ElemComboBox* m_midiIn = nullptr;
		juceRmlUi::ElemComboBox* m_midiOut = nullptr;
	};
}
