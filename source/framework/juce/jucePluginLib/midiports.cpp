#include "midiports.h"

#include <set>

#include "processor.h"

#include "dsp56kBase/threadtools.h"

#include "juce_audio_devices/juce_audio_devices.h"

#include "synthLib/midiBufferParser.h"

namespace pluginLib
{
	namespace
	{
		// The instances that reopen their ports when the system's MIDI devices change. The change arrives on the message
		// thread, a host may delete a plugin on another one: the destructor waits for a change that is handled
		std::mutex g_instancesMutex;
		std::set<MidiPorts*> g_instances;

		// One for all instances, made on the message thread. Never deleted: JUCE's broadcaster is a static of its own,
		// which of the two goes first at unload is unknown. JUCE reports a change only if the device lists differ, a
		// device that comes back fast with its ports in the same order goes unnoticed
		juce::MidiDeviceListConnection* g_deviceListConnection = nullptr;

		// where a device is in a list of the system's MIDI devices, -1: not there
		int indexOf(const juce::Array<juce::MidiDeviceInfo>& _devices, const juce::String& _id)
		{
			for (int i = 0; i < _devices.size(); ++i)
			{
				if (_devices.getReference(i).identifier == _id)
					return i;
			}
			return -1;
		}
	}

	void MidiPorts::listenToDeviceList()
	{
		if (g_deviceListConnection)
			return;

		g_deviceListConnection = new juce::MidiDeviceListConnection(juce::MidiDeviceListConnection::make([]
		{
			onDevicesChanged();
		}));
	}

	void MidiPorts::onDevicesChanged()
	{
		const auto inputs = juce::MidiInput::getAvailableDevices();
		const auto outputs = juce::MidiOutput::getAvailableDevices();

		const std::lock_guard lock(g_instancesMutex);

		// All close first: JUCE shares a Windows MIDI input between all that open it, and opens the device anew only
		// once none of them uses it
		for (auto* ports : g_instances)
			ports->closeMovedPorts(inputs, outputs);

		for (auto* ports : g_instances)
			ports->openClosedPorts();
	}

	MidiPorts::MidiPorts(Processor& _processor) : m_processor(_processor)
	{
		const std::lock_guard lock(g_instancesMutex);

		g_instances.insert(this);

		if (juce::MessageManager::existsAndIsCurrentThread())
		{
			listenToDeviceList();
		}
		else
		{
			juce::MessageManager::callAsync([]
			{
				const std::lock_guard l(g_instancesMutex);
				listenToDeviceList();
			});
		}
	}

	MidiPorts::~MidiPorts()
	{
		{
			const std::lock_guard lock(g_instancesMutex);
			g_instances.erase(this);
		}

		close();
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

		// the selection, also while its device is away
		const std::lock_guard lock(m_mutexSelection);
		_binaryStream.write(m_selectedInput.toStdString());
		_binaryStream.write(m_selectedOutput.toStdString());
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
		const std::lock_guard lock(m_mutexSelection);
		m_selectedOutput = _out;
		return openMidiOutput(_out);
	}

	bool MidiPorts::setMidiInput(const juce::String& _in)
	{
		const std::lock_guard lock(m_mutexSelection);
		m_selectedInput = _in;
		return openMidiInput(_in);
	}

	void MidiPorts::closeMovedPorts(const juce::Array<juce::MidiDeviceInfo>& _inputs, const juce::Array<juce::MidiDeviceInfo>& _outputs)
	{
		const std::lock_guard lock(m_mutexSelection);

		if (m_midiInput && indexOf(_inputs, m_selectedInput) != m_inputIndex)
			openMidiInput({});

		if (m_midiOutput && indexOf(_outputs, m_selectedOutput) != m_outputIndex)
			openMidiOutput({});
	}

	void MidiPorts::openClosedPorts()
	{
		const std::lock_guard lock(m_mutexSelection);

		if (!m_midiInput && !m_selectedInput.isEmpty())
			openMidiInput(m_selectedInput);

		if (!m_midiOutput && !m_selectedOutput.isEmpty())
			openMidiOutput(m_selectedOutput);
	}

	bool MidiPorts::openMidiOutput(const juce::String& _out)
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

		// For an output it does not find, JUCE opens the Microsoft GS Wavetable Synth on Windows. A device that is away
		// opens once it is back
		const auto index = indexOf(juce::MidiOutput::getAvailableDevices(), _out);
		if (index < 0)
			return false;

		std::lock_guard lock(m_mutexOutput);

		m_midiOutput = juce::MidiOutput::openDevice(_out);
		if (m_midiOutput != nullptr)
		{
			m_outputIndex = index;
			m_midiOutput->startBackgroundThread();
			m_threadOutput.reset(new std::thread([this] { senderThread(); }));
			m_outputOpen = true;
			return true;
		}
		return false;
	}

	bool MidiPorts::openMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
			m_midiInput = nullptr;
		}

		if(_in.isEmpty())
			return false;

		// a device that is away opens once it is back
		const auto index = indexOf(juce::MidiInput::getAvailableDevices(), _in);
		if (index < 0)
			return false;

		m_midiInput = juce::MidiInput::openDevice(_in, this);
		if (m_midiInput != nullptr)
		{
			m_inputIndex = index;
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
