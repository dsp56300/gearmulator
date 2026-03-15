#pragma once

#include "baseLib/event.h"

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
		explicit MidiPorts(Editor& _editor, Processor& _processor);

		static void createMidiInputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);
		static void createMidiOutputMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);
		static void createMidiPortsMenu(juceRmlUi::Menu& _menu, pluginLib::MidiPorts&);

		static void initInputComboBox(Processor& _processor, juceRmlUi::ElemComboBox* _combo);
		static void initOutputComboBox(Processor& _processor, juceRmlUi::ElemComboBox* _combo);

		void refreshSelection();

	private:
		pluginLib::MidiPorts& getMidiPorts() const;
		static pluginLib::MidiPorts& getMidiPorts(Processor& _processor);

		void showMidiPortFailedMessage(const char* _name) const;
		static void showMidiPortFailedMessage(const pluginLib::Processor& _processor, const char* _name);

		void updateMidiInput(int _index) const;
		static void updateMidiInput(Processor& _processor, juceRmlUi::ElemComboBox* _combo, int _index);

		void updateMidiOutput(int _index) const;
		static void updateMidiOutput(Processor& _processor, juceRmlUi::ElemComboBox* _combo, int _index);

		Processor& m_processor;

		juceRmlUi::ElemComboBox* m_midiIn = nullptr;
		juceRmlUi::ElemComboBox* m_midiOut = nullptr;

		baseLib::EventListener<> m_onSettingsClosed;
	};
}
