#pragma once

namespace juce
{
	class AudioDeviceManager;
	class ComboBox;
}

namespace genericVirusUI
{
	class VirusEditor;

	class MidiPorts
	{
	public:
		explicit MidiPorts(VirusEditor& _editor);
		~MidiPorts();

	private:
		VirusEditor& m_editor;

		juce::ComboBox* m_midiIn = nullptr;
		juce::ComboBox* m_midiOut = nullptr;

		juce::AudioDeviceManager* deviceManager = nullptr;
		int m_lastInputIndex = 0;
		int m_lastOutputIndex = 0;

		void updateMidiInput(int _index);
		void updateMidiOutput(int _index);

	};
}
