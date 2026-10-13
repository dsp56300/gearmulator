#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "dsp56kBase/ringbuffer.h"

#include "juce_audio_devices/juce_audio_devices.h"

namespace synthLib
{
	struct SMidiEvent;
}

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

		void send(juce::MidiMessage&& _message)
		{
			push(std::move(_message));
		}

		void send(const juce::MidiMessage& _message)
		{
			push(juce::MidiMessage(_message));
		}

		void send(const synthLib::SMidiEvent& _message)
		{
			return send(toJuceMidiMessage(_message));
		}

		void close();

		static juce::MidiMessage toJuceMidiMessage(const synthLib::SMidiEvent& _e);

		bool isMidiOutValid() const
		{
			return m_outputOpen;
		}

	private:
	    void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override;

		void senderThread();

		// The audio thread, the editor and MIDI learn feedback all send. A full queue drops the message rather than
		// stall the audio thread behind a port that sends slower than it is fed
		void push(juce::MidiMessage&& _message)
		{
			if (!m_outputOpen)
				return;
			std::lock_guard lock(m_mutexProducers);
			if (m_midiOutMessages.full())
				return;
			m_midiOutMessages.push_back(std::move(_message));
		}

		Processor& m_processor;

		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::unique_ptr<juce::AudioDeviceManager> m_deviceManager;

		dsp56k::RingBuffer<juce::MidiMessage, 128, true> m_midiOutMessages;
		// the ring takes one producer at a time
		std::mutex m_mutexProducers;
		// guards m_midiOutput for everyone but the sender thread, which only runs while the port stays as it is
		std::mutex m_mutexOutput;
		std::atomic<bool> m_outputOpen{false};
		std::unique_ptr<std::thread> m_threadOutput;
	};
}
