#pragma once

#include <memory>

#include "juce_audio_devices/juce_audio_devices.h"

namespace baseLib
{
	class ChunkReader;
	class BinaryStream;
}

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

		auto& getProcessor() const { return m_processor; }

		juce::MidiOutput* getMidiOutput() const;
		juce::MidiInput* getMidiInput() const;

		bool setMidiOutput(const juce::String& _out);
		bool setMidiInput(const juce::String& _in);

		juce::String getInputId() const;
		juce::String getOutputId() const;

		void saveChunkData(baseLib::BinaryStream& _binaryStream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

	private:
	    void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override;

		Processor& m_processor;

		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::unique_ptr<juce::AudioDeviceManager> m_deviceManager;
	};
}
