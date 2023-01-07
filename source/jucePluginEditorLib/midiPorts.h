#pragma once

namespace genericUI
{
	class Editor;
}

namespace juce
{
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
		~MidiPorts();

	private:
		Processor& m_processor;

		juce::ComboBox* m_midiIn = nullptr;
		juce::ComboBox* m_midiOut = nullptr;

		juce::AudioDeviceManager* deviceManager = nullptr;
		int m_lastInputIndex = 0;
		int m_lastOutputIndex = 0;

		void updateMidiInput(int _index);
		void updateMidiOutput(int _index);
	};
}
