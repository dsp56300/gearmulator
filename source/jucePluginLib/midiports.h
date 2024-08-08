#pragma once

#include <memory>

#include "juce_audio_devices/juce_audio_devices.h"
#include "synthLib/binarystream.h"

namespace juce
{
	class String;
}

namespace pluginLib
{
	class Processor;

	class MidiPorts : juce::MidiInputCallback
	{
	public:
		MidiPorts(Processor& _processor);
		MidiPorts(MidiPorts&&) = delete;
		MidiPorts(const MidiPorts&) = delete;

		~MidiPorts() override;

		MidiPorts& operator = (const MidiPorts&) = delete;
		MidiPorts& operator = (MidiPorts&&) = delete;

		bool setMidiOutput(const juce::String& _out);
		juce::MidiOutput* getMidiOutput() const;
		bool setMidiInput(const juce::String& _in);
		juce::MidiInput* getMidiInput() const;

		void saveChunkData(synthLib::BinaryStream& _binaryStream) const;
		void loadChunkData(synthLib::ChunkReader& _cr);

	private:
	    void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override;

		Processor& m_processor;

		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::unique_ptr<juce::AudioDeviceManager> m_deviceManager;
	};
}
