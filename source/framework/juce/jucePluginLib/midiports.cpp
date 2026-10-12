#include "midiports.h"

#include "processor.h"

#include "dsp56kBase/threadtools.h"

#include "juce_audio_devices/juce_audio_devices.h"

#include "synthLib/midiBufferParser.h"

namespace pluginLib
{
	MidiPorts::MidiPorts(Processor& _processor) : m_processor(_processor)
	{
	}

	MidiPorts::~MidiPorts()
	{
		close();
		m_deviceManager.reset();
	}

	juce::MidiOutput *MidiPorts::getMidiOutput() const
	{
		return m_midiOutput.get();
	}

	juce::MidiInput *MidiPorts::getMidiInput() const
	{
		return m_midiInput.get();
	}

	juce::String MidiPorts::getInputId() const
	{
		return getMidiInput() != nullptr ? getMidiInput()->getIdentifier() : juce::String();
	}

	juce::String MidiPorts::getOutputId() const
	{
		return getMidiOutput() != nullptr ? getMidiOutput()->getIdentifier() : juce::String();
	}

	void MidiPorts::saveChunkData(baseLib::BinaryStream& _binaryStream) const
	{
		baseLib::ChunkWriter cw(_binaryStream, "mpIO", 1);

		if(m_midiInput)
			_binaryStream.write(m_midiInput->getIdentifier().toStdString());
		else
			_binaryStream.write(std::string());
		if(m_midiOutput)
			_binaryStream.write(m_midiOutput->getIdentifier().toStdString());
		else
			_binaryStream.write(std::string());
	}

	void MidiPorts::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("mpIO", 1, [&](baseLib::BinaryStream& _data, uint32_t)
		{
			const auto input = _data.readString();
			const auto output = _data.readString();

			setMidiInput(input);
			setMidiOutput(output);
		});
	}

	void MidiPorts::close()
	{
		setMidiInput({});
		setMidiOutput({});
	}

	juce::MidiMessage MidiPorts::toJuceMidiMessage(const synthLib::SMidiEvent& _e)
	{
	    if(!_e.sysex.empty())
	    {
		    assert(_e.sysex.front() == 0xf0);
		    assert(_e.sysex.back() == 0xf7);

		    return {_e.sysex.data(), static_cast<int>(_e.sysex.size()), 0.0};
	    }
	    const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(_e.a);
	    if(len == 1)
		    return {_e.a, 0.0};
	    if(len == 2)
		    return {_e.a, _e.b, 0.0};
	    return {_e.a, _e.b, _e.c, 0.0};
	}

	bool MidiPorts::setMidiOutput(const juce::String& _out)
	{
		m_outputOpen = false;

		{
			// send dummy to wakeup thread. Only if there is one: the dummy is an empty SysEx, which would wait in the
			// queue and go out to the next port opened
			std::lock_guard lock(m_mutexProducers);
			if (m_threadOutput)
				m_midiOutMessages.push_back(juce::MidiMessage());
		}

		// the sender uses the port without the lock, it has to be gone before the port is
		if (m_threadOutput)
		{
			m_threadOutput->join();
			m_threadOutput.reset();
		}

		{
			std::lock_guard lock(m_mutexOutput);
			if (m_midiOutput != nullptr)
			{
				if (m_midiOutput->isBackgroundThreadRunning())
					m_midiOutput->stopBackgroundThread();
			}
			m_midiOutput = nullptr;
		}

		{
			// a send that got past the open check while the port closed must not go out to the next one
			std::lock_guard lock(m_mutexProducers);
			while (!m_midiOutMessages.empty())
				m_midiOutMessages.pop_front();
		}

		if(_out.isEmpty())
			return false;

		std::lock_guard lock(m_mutexOutput);

		m_midiOutput = juce::MidiOutput::openDevice(_out);
		if (m_midiOutput != nullptr)
		{
			m_midiOutput->startBackgroundThread();
			m_threadOutput.reset(new std::thread([this] { senderThread(); }));
			m_outputOpen = true;
			return true;
		}
		return false;
	}

	bool MidiPorts::setMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
			m_midiInput = nullptr;
		}

		if(_in.isEmpty())
			return false;

		if(!m_deviceManager)
			m_deviceManager.reset(new juce::AudioDeviceManager());

		if (!m_deviceManager->isMidiInputDeviceEnabled(_in))
			m_deviceManager->setMidiInputDeviceEnabled(_in, true);

		m_midiInput = juce::MidiInput::openDevice(_in, this);
		if (m_midiInput != nullptr)
		{
			m_midiInput->start();
			return true;
		}
		return false;
	}

	void MidiPorts::handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message)
	{
		m_processor.handleIncomingMidiMessage(_source, _message);
	}

	void MidiPorts::senderThread()
	{
		dsp56k::ThreadTools::setCurrentThreadName("MIdiOutputSender");

		// setMidiOutput() changes the port only while this thread is not running, so it is used without the lock: a
		// SysEx can take a while on the wire, and whoever wants the lock meanwhile would wait for it
		auto* out = m_midiOutput.get();

		while (true)
		{
			auto msg = m_midiOutMessages.pop_front();

			if (!m_outputOpen)
				break;

			out->sendMessageNow(msg);
		}
	}
}
