#include "midiports.h"

#include "processor.h"

#include "juce_audio_devices/juce_audio_devices.h"

namespace pluginLib
{
	MidiPorts::MidiPorts(Processor& _processor) : m_processor(_processor)
	{
	}

	MidiPorts::~MidiPorts()
	{
		setMidiInput({});
		setMidiOutput({});

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

	bool MidiPorts::setMidiOutput(const juce::String& _out)
	{
		if (m_midiOutput != nullptr && m_midiOutput->isBackgroundThreadRunning())
		{
			m_midiOutput->stopBackgroundThread();
		}
		if(_out.isEmpty())
			return false;
		m_midiOutput = juce::MidiOutput::openDevice(_out);
		if (m_midiOutput != nullptr)
		{
			m_midiOutput->startBackgroundThread();
			return true;
		}
		return false;
	}

	bool MidiPorts::setMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
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
}
