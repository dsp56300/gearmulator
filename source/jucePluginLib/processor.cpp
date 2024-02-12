#include "processor.h"
#include "dummydevice.h"
#include "types.h"

#include "../synthLib/deviceException.h"
#include "../synthLib/os.h"
#include "../synthLib/binarystream.h"

#include "dsp56kEmu/logging.h"

namespace synthLib
{
	class DeviceException;
}

namespace pluginLib
{
	constexpr char g_saveMagic[] = "DSP56300";
	constexpr uint32_t g_saveVersion = 1;

	Processor::Processor(const BusesProperties& _busesProperties) : juce::AudioProcessor(_busesProperties)
	{
	}

	Processor::~Processor()
	{
		m_controller.reset();
		m_plugin.reset();
		m_device.reset();
	}

	void Processor::getLastMidiOut(std::vector<synthLib::SMidiEvent>& dst)
	{
		juce::ScopedLock lock(getCallbackLock());
		std::swap(dst, m_midiOut);
		m_midiOut.clear();
	}

	void Processor::addMidiEvent(const synthLib::SMidiEvent& ev)
	{
		getPlugin().addMidiEvent(ev);
	}

	juce::MidiOutput *Processor::getMidiOutput() const { return m_midiOutput.get(); }
	juce::MidiInput *Processor::getMidiInput() const { return m_midiInput.get(); }

	bool Processor::setMidiOutput(const juce::String& _out)
	{
		if (m_midiOutput != nullptr && m_midiOutput->isBackgroundThreadRunning())
		{
			m_midiOutput->stopBackgroundThread();
		}
		m_midiOutput = juce::MidiOutput::openDevice(_out);
		if (m_midiOutput != nullptr)
		{
			m_midiOutput->startBackgroundThread();
			return true;
		}
		return false;
	}

	bool Processor::setMidiInput(const juce::String& _in)
	{
		if (m_midiInput != nullptr)
		{
			m_midiInput->stop();
		}
		m_midiInput = juce::MidiInput::openDevice(_in, this);
		if (m_midiInput != nullptr)
		{
			m_midiInput->start();
			return true;
		}
		return false;
	}

	void Processor::handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message)
	{
		const auto* raw = message.getSysExData();
		if (raw)
		{
			const auto count = message.getSysExDataSize();
			auto syx = pluginLib::SysEx();
			syx.push_back(0xf0);
			for (int i = 0; i < count; i++)
			{
				syx.push_back(raw[i]);
			}
			syx.push_back(0xf7);
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			sm.sysex = syx;
			getController().parseSysexMessage(syx);

			addMidiEvent(sm);

			if (m_midiOutput)
			{
				std::vector<synthLib::SMidiEvent> data;
				getLastMidiOut(data);
				if (!data.empty())
				{
					const auto msg = juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));

					m_midiOutput->sendMessageNow(msg);
				}
			}
		}
		else
		{
			const auto count = message.getRawDataSize();
			const auto* rawData = message.getRawData();
			if (count >= 1 && count <= 3)
			{
				synthLib::SMidiEvent sm;
				sm.source = synthLib::MidiEventSourcePlugin;
				sm.a = rawData[0];
				sm.b = count > 1 ? rawData[1] : 0;
				sm.c = count > 2 ? rawData[2] : 0;
				addMidiEvent(sm);
			}
			else
			{
				synthLib::SMidiEvent sm;
				sm.source = synthLib::MidiEventSourcePlugin;
				auto syx = SysEx();
				for (int i = 0; i < count; i++)
				{
					syx.push_back(rawData[i]);
				}
				sm.sysex = syx;
				addMidiEvent(sm);
			}
		}
	}

	pluginLib::Controller& Processor::getController()
	{
	    if (m_controller == nullptr)
	        m_controller.reset(createController());

	    return *m_controller;
	}

	synthLib::Plugin& Processor::getPlugin()
	{
		if(m_plugin)
			return *m_plugin;

		try
		{
			m_device.reset(createDevice());
		}
		catch(const synthLib::DeviceException& e)
		{
			LOG("Failed to create device: " << e.what());

			std::string msg = e.what();

			m_deviceError = e.errorCode();

			if(e.errorCode() == synthLib::DeviceError::FirmwareMissing)
			{
				msg += "\n\n";
				msg += "The firmware file needs to be located next to the plugin.";
				msg += "\n\n";
				msg += "The plugin was loaded from path:\n\n" + synthLib::getModulePath() + "\n\nCopy the requested file to this path and reload the plugin.";
			}
			juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Device Initialization failed", msg);
		}

		if(!m_device)
		{
			m_device.reset(new DummyDevice());
		}

		m_plugin.reset(new synthLib::Plugin(m_device.get()));

		return *m_plugin;
	}

	bool Processor::setLatencyBlocks(uint32_t _blocks)
	{
		if (!getPlugin().setLatencyBlocks(_blocks))
			return false;
		updateLatencySamples();
		return true;
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		synthLib::BinaryStream s;

		{
			synthLib::ChunkWriter cw(s, "GAIN", 1);
			s.write<uint32_t>(1);	// version
			s.write(m_inputGain);
			s.write(m_outputGain);
		}

		s.toVector(_targetBuffer, true);
	}

	void Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		auto readGain = [this](synthLib::BinaryStream& _s)
		{
			const auto version = _s.read<uint32_t>();
			if (version != 1)
				return;
			m_inputGain = _s.read<float>();
			m_outputGain = _s.read<float>();
		};

		// In Vavra, the only data we had was the gain parameters
		if(_sourceBuffer.size() == sizeof(float) * 2 + sizeof(uint32_t))
		{
			synthLib::BinaryStream ss(_sourceBuffer);
			readGain(ss);
		}

		synthLib::BinaryStream s(_sourceBuffer);
		synthLib::ChunkReader cr(s);

		cr.add("GAIN", 1, [readGain](synthLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			readGain(_binaryStream);
		});

		cr.tryRead();
	}

	//==============================================================================
	void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
	{
		// Use this method as the place to do any pre-playback
		// initialisation that you need..
		getPlugin().setSamplerate(static_cast<float>(sampleRate));
		getPlugin().setBlockSize(samplesPerBlock);

		updateLatencySamples();
	}

	void Processor::releaseResources()
	{
		// When playback stops, you can use this as an opportunity to free up any
		// spare memory, etc.
	}

	//==============================================================================
	void Processor::getStateInformation (juce::MemoryBlock& destData)
	{
	    // You should use this method to store your parameters in the memory block.
	    // You could do that either as raw data, or use the XML or ValueTree classes
	    // as intermediaries to make it easy to save and load complex data.
#if !SYNTHLIB_DEMO_MODE
		std::vector<uint8_t> buffer;
		getPlugin().getState(buffer, synthLib::StateTypeGlobal);

		PluginStream ss;
		ss.write(g_saveMagic);
		ss.write(g_saveVersion);
		ss.write(buffer);
		buffer.clear();
		saveCustomData(buffer);
		ss.write(buffer);

		std::vector<uint8_t> buf;
		ss.toVector(buf);

		destData.append(buf.data(), buf.size());
#endif
	}

	void Processor::setStateInformation (const void* _data, const int _sizeInBytes)
	{
#if !SYNTHLIB_DEMO_MODE
		// You should use this method to restore your parameters from this memory block,
	    // whose contents will have been created by the getStateInformation() call.
		setState(_data, _sizeInBytes);
#endif
	}

	void Processor::getCurrentProgramStateInformation(juce::MemoryBlock& destData)
	{
#if !SYNTHLIB_DEMO_MODE
		std::vector<uint8_t> state;
		getPlugin().getState(state, synthLib::StateTypeCurrentProgram);
		destData.append(state.data(), state.size());
#endif
	}

	void Processor::setCurrentProgramStateInformation(const void* data, int sizeInBytes)
	{
#if !SYNTHLIB_DEMO_MODE
		setState(data, sizeInBytes);
#endif
	}

#if !SYNTHLIB_DEMO_MODE
	void Processor::setState(const void* _data, const size_t _sizeInBytes)
	{
		if(_sizeInBytes < 1)
			return;

		std::vector<uint8_t> state;
		state.resize(_sizeInBytes);
		memcpy(state.data(), _data, _sizeInBytes);

		PluginStream ss(state);

		if (ss.checkString(g_saveMagic))
		{
			try
			{
				const std::string magic = ss.readString();

				if (magic != g_saveMagic)
					return;

				const auto version = ss.read<uint32_t>();

				if (version != g_saveVersion)
					return;

				std::vector<uint8_t> buffer;
				ss.read(buffer);
				getPlugin().setState(buffer);
				ss.read(buffer);

				if(!buffer.empty())
				{
					try
					{
						loadCustomData(buffer);
					}
					catch (std::range_error&)
					{
					}
				}
			}
			catch (std::range_error& e)
			{
				LOG("Failed to read state: " << e.what());
				return;
			}
		}
		else
		{
			getPlugin().setState(state);
		}

		if (hasController())
			getController().onStateLoaded();
	}
#endif

	//==============================================================================

	int Processor::getNumPrograms()
	{
		return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
				  // so this should be at least 1, even if you're not really implementing programs.
	}

	int Processor::getCurrentProgram()
	{
		return 0;
	}

	void Processor::setCurrentProgram(int _index)
	{
		juce::ignoreUnused(_index);
	}

	const juce::String Processor::getProgramName(int _index)
	{
		juce::ignoreUnused(_index);
		return "default";
	}

	void Processor::changeProgramName(int _index, const juce::String& _newName)
	{
		juce::ignoreUnused(_index, _newName);
	}

	double Processor::getTailLengthSeconds() const
	{
		return 0.0f;
	}
}
