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

		// A port whose device is away stays selected and opens once the device is back. When the system's MIDI devices
		// change, the ports whose devices went away, came back or moved in the lists reopen by themselves: a device
		// plugged in again, or back after a USB hiccup, has new handles, the old ones send and get nothing
		bool setMidiOutput(const juce::String& _out);
		bool setMidiInput(const juce::String& _in);

		// the ports that are open
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

		// setMidiInput() and setMidiOutput() without changing the selection
		bool openMidiInput(const juce::String& _in);
		bool openMidiOutput(const juce::String& _out);

		// The system's MIDI devices changed, for all instances. Closes the ports whose devices are not where they were
		// in the lists, then opens the selected ports that are closed
		static void listenToDeviceList();
		static void onDevicesChanged();
		void closeMovedPorts(const juce::Array<juce::MidiDeviceInfo>& _inputs, const juce::Array<juce::MidiDeviceInfo>& _outputs);
		void openClosedPorts();

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

		// the ports the user or a state selected, open or not: their device may be away for a while
		juce::String m_selectedInput;
		juce::String m_selectedOutput;
		// selecting and reopening happen on the message thread and wherever a host loads a state
		mutable std::mutex m_mutexSelection;
		// where the open ports' devices were in the system's lists: one that is listed elsewhere now went away and came
		// back, plugged in again or after a USB hiccup
		int m_inputIndex = -1;
		int m_outputIndex = -1;

		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};

		dsp56k::RingBuffer<juce::MidiMessage, 128, true> m_midiOutMessages;
		// the ring takes one producer at a time
		std::mutex m_mutexProducers;
		// guards m_midiOutput for everyone but the sender thread, which only runs while the port stays as it is
		std::mutex m_mutexOutput;
		std::atomic<bool> m_outputOpen{false};
		std::unique_ptr<std::thread> m_threadOutput;
	};
}
