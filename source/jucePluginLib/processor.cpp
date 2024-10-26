#include "processor.h"
#include "dummydevice.h"
#include "tools.h"
#include "types.h"

#include "baseLib/binarystream.h"

#include "synthLib/deviceException.h"
#include "synthLib/os.h"
#include "synthLib/midiBufferParser.h"

#include "dsp56kEmu/fastmath.h"

#include "dsp56kEmu/logging.h"
#include "synthLib/romLoader.h"

namespace synthLib
{
	class DeviceException;
}

namespace pluginLib
{
	constexpr char g_saveMagic[] = "DSP56300";
	constexpr uint32_t g_saveVersion = 2;

	Processor::Processor(const BusesProperties& _busesProperties, Properties _properties) : juce::AudioProcessor(_busesProperties), m_properties(std::move(_properties)), m_midiPorts(*this)
	{
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(true));
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(false));
		synthLib::RomLoader::addSearchPath(getPublicRomFolder());
	}

	Processor::~Processor()
	{
		destroyController();
		m_plugin.reset();
		m_device.reset();
	}

	void Processor::addMidiEvent(const synthLib::SMidiEvent& ev)
	{
		getPlugin().addMidiEvent(ev);
	}

	void Processor::handleIncomingMidiMessage(juce::MidiInput *_source, const juce::MidiMessage &_message)
	{
		synthLib::SMidiEvent sm(synthLib::MidiEventSource::PhysicalInput);

		const auto* raw = _message.getSysExData();
		if (raw)
		{
			const auto count = _message.getSysExDataSize();
			auto syx = pluginLib::SysEx();
			syx.push_back(0xf0);
			for (int i = 0; i < count; i++)
			{
				syx.push_back(raw[i]);
			}
			syx.push_back(0xf7);
			sm.sysex = std::move(syx);

			getController().enqueueMidiMessages({sm});
			addMidiEvent(sm);
		}
		else
		{
			const auto count = _message.getRawDataSize();
			const auto* rawData = _message.getRawData();
			if (count >= 1 && count <= 3)
			{
				sm.a = rawData[0];
				sm.b = count > 1 ? rawData[1] : 0;
				sm.c = count > 2 ? rawData[2] : 0;
			}
			else
			{
				auto syx = SysEx();
				for (int i = 0; i < count; i++)
					syx.push_back(rawData[i]);
				sm.sysex = syx;
			}

			getController().enqueueMidiMessages({sm});
			addMidiEvent(sm);
		}
	}

	Controller& Processor::getController()
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
			if(!m_device->isValid())
				throw synthLib::DeviceException(synthLib::DeviceError::Unknown, "Device initialization failed");
		}
		catch(const synthLib::DeviceException& e)
		{
			LOG("Failed to create device: " << e.what());

			// Juce loads the LV2/VST3 versions of the plugin as part of the build process, if we open a message box in this case, the build process gets stuck
			const auto host = juce::PluginHostType::getHostPath();
			if(!Tools::isHeadless())
			{
				std::string msg = e.what();

				m_deviceError = e.errorCode();

				if(e.errorCode() == synthLib::DeviceError::FirmwareMissing)
				{
					msg += "\n\n";
					msg += "The firmware file needs to be copied to\n";
					msg += synthLib::validatePath(getPublicRomFolder()) + "\n";
					msg += "\n";
					msg += "The target folder will be opened once you click OK. Copy the firmware to this folder and reload the plugin.";
#ifdef _DEBUG
					msg += "\n\n" + std::string("[Debug] Host ") + host.toStdString() + "\n\n";
#endif
				}
				juce::Timer::callAfterDelay(2000, [this, msg]
				{
					juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
						"Device Initialization failed", msg, nullptr, 
						juce::ModalCallbackFunction::create([this](int)
						{
							const auto path = juce::File(getPublicRomFolder());
							(void)path.createDirectory();
							path.revealToUser();
						})
					);
				});
			}
		}

		if(!m_device)
		{
			m_device.reset(new DummyDevice());
		}

		m_device->setDspClockPercent(m_dspClockPercent);

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

	void Processor::updateLatencySamples()
	{
		if(getProperties().isSynth)
			setLatencySamples(getPlugin().getLatencyMidiToOutput());
		else
			setLatencySamples(getPlugin().getLatencyInputToOutput());
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		baseLib::BinaryStream s;
		saveChunkData(s);
		s.toVector(_targetBuffer, true);
	}

	void Processor::saveChunkData(baseLib::BinaryStream& s)
	{
		{
			std::vector<uint8_t> buffer;
			getPlugin().getState(buffer, synthLib::StateTypeGlobal);

			baseLib::ChunkWriter cw(s, "MIDI", 1);
			s.write(buffer);
		}
		{
			baseLib::ChunkWriter cw(s, "GAIN", 1);
			s.write<uint32_t>(1);	// version
			s.write(m_inputGain);
			s.write(m_outputGain);
		}

		if(m_dspClockPercent != 100)
		{
			baseLib::ChunkWriter cw(s, "DSPC", 1);
			s.write(m_dspClockPercent);
		}

		if(m_preferredDeviceSamplerate > 0)
		{
			baseLib::ChunkWriter cw(s, "DSSR", 1);
			s.write(m_preferredDeviceSamplerate);
		}

		m_midiPorts.saveChunkData(s);
	}

	bool Processor::loadCustomData(const std::vector<uint8_t>& _sourceBuffer)
	{
		if(_sourceBuffer.empty())
			return true;

		// In Vavra, the only data we had was the gain parameters
		if(_sourceBuffer.size() == sizeof(float) * 2 + sizeof(uint32_t))
		{
			baseLib::BinaryStream ss(_sourceBuffer);
			readGain(ss);
			return true;
		}

		baseLib::BinaryStream s(_sourceBuffer);
		baseLib::ChunkReader cr(s);

		loadChunkData(cr);

		return _sourceBuffer.empty() || (cr.tryRead() && cr.numRead() > 0);
	}

	void Processor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("MIDI", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			std::vector<uint8_t> buffer;
			_binaryStream.read(buffer);
			getPlugin().setState(buffer);
		});

		_cr.add("GAIN", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			readGain(_binaryStream);
		});

		_cr.add("DSPC", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			auto p = _binaryStream.read<uint32_t>();
			p = dsp56k::clamp<uint32_t>(p, 50, 200);
			setDspClockPercent(p);
		});

		_cr.add("DSSR", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			const auto sr = _binaryStream.read<float>();
			setPreferredDeviceSamplerate(sr);
		});

		m_midiPorts.loadChunkData(_cr);
	}

	void Processor::readGain(baseLib::BinaryStream& _s)
	{
		const auto version = _s.read<uint32_t>();
		if (version != 1)
			return;
		m_inputGain = _s.read<float>();
		m_outputGain = _s.read<float>();
	}

	bool Processor::setDspClockPercent(const uint32_t _percent)
	{
		if(!m_device)
			return false;
		if(!m_device->setDspClockPercent(_percent))
			return false;
		m_dspClockPercent = _percent;
		return true;
	}

	uint32_t Processor::getDspClockPercent() const
	{
		if(!m_device)
			return m_dspClockPercent;
		return m_device->getDspClockPercent();
	}

	uint64_t Processor::getDspClockHz() const
	{
		if(!m_device)
			return 0;
		return m_device->getDspClockHz();
	}

	bool Processor::setPreferredDeviceSamplerate(const float _samplerate)
	{
		m_preferredDeviceSamplerate = _samplerate;

		if(!m_device)
			return false;

		return getPlugin().setPreferredDeviceSamplerate(_samplerate);
	}

	float Processor::getPreferredDeviceSamplerate() const
	{
		return m_preferredDeviceSamplerate;
	}

	std::vector<float> Processor::getDeviceSupportedSamplerates() const
	{
		if(!m_device)
			return {};
		return m_device->getSupportedSamplerates();
	}

	std::vector<float> Processor::getDevicePreferredSamplerates() const
	{
		if(!m_device)
			return {};
		return m_device->getPreferredSamplerates();
	}

	std::optional<std::pair<const char*, uint32_t>> Processor::findResource(const std::string& _filename) const
	{
		const auto& bd = m_properties.binaryData;

		for(uint32_t i=0; i<bd.listSize; ++i)
		{
			if (bd.originalFileNames[i] != _filename)
				continue;

			int size = 0;
			const auto res = bd.getNamedResourceFunc(bd.namedResourceList[i], size);
			return {std::make_pair(res, static_cast<uint32_t>(size))};
		}
		return {};
	}

	std::string Processor::getPublicRomFolder() const
	{
		return Tools::getPublicDataFolder(getProperties().name) + "roms/";
	}

	void Processor::destroyController()
	{
		m_controller.reset();
	}

	//==============================================================================
	void Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
	{
		// Use this method as the place to do any pre-playback
		// initialisation that you need
		m_hostSamplerate = static_cast<float>(sampleRate);

		getPlugin().setHostSamplerate(static_cast<float>(sampleRate), m_preferredDeviceSamplerate);
		getPlugin().setBlockSize(samplesPerBlock);

		updateLatencySamples();
	}

	void Processor::releaseResources()
	{
		// When playback stops, you can use this as an opportunity to free up any
		// spare memory, etc.
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _busesLayout) const
	{
	    // This is the place where you check if the layout is supported.
	    // In this template code we only support mono or stereo.
	    // Some plugin hosts, such as certain GarageBand versions, will only
	    // load plugins that support stereo bus layouts.
	    if (_busesLayout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
	        return false;

	    // This checks if the input is stereo
	    if (_busesLayout.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
	        return false;

	    return true;
	}

	//==============================================================================
	void Processor::getStateInformation (juce::MemoryBlock& destData)
	{
	    // You should use this method to store your parameters in the memory block.
	    // You could do that either as raw data, or use the XML or ValueTree classes
	    // as intermediaries to make it easy to save and load complex data.
#if !SYNTHLIB_DEMO_MODE
		PluginStream ss;
		ss.write(g_saveMagic);
		ss.write(g_saveVersion);
		std::vector<uint8_t> buffer;
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
		
	const juce::String Processor::getName() const
	{
	    return getProperties().name;
	}

	bool Processor::acceptsMidi() const
	{
		return getProperties().wantsMidiInput;
	}

	bool Processor::producesMidi() const
	{
		return getProperties().producesMidiOut;
	}

	bool Processor::isMidiEffect() const
	{
		return getProperties().isMidiEffect;
	}

	void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
	{
	    juce::ScopedNoDenormals noDenormals;
	    const auto totalNumInputChannels  = getTotalNumInputChannels();
	    const auto totalNumOutputChannels = getTotalNumOutputChannels();

	    const int numSamples = buffer.getNumSamples();

	    // In case we have more outputs than inputs, this code clears any output
	    // channels that didn't contain input data, (because these aren't
	    // guaranteed to be empty - they may contain garbage).
	    // This is here to avoid people getting screaming feedback
	    // when they first compile a plugin, but obviously you don't need to keep
	    // this code if your algorithm always overwrites all the output channels.
	    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
			buffer.clear (i, 0, numSamples);

	    // This is the place where you'd normally do the guts of your plugin's
	    // audio processing...
	    // Make sure to reset the state if your inner loop is processing
	    // the samples and the outer loop is handling the channels.
	    // Alternatively, you can process the samples with the channels
	    // interleaved by keeping the same state.

	    synthLib::TAudioInputs inputs{};
	    synthLib::TAudioOutputs outputs{};

	    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    		inputs[channel] = buffer.getReadPointer(channel);

	    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    		outputs[channel] = buffer.getWritePointer(channel);

		for(const auto metadata : midiMessages)
		{
			const auto message = metadata.getMessage();

			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);

			if(message.isSysEx() || message.getRawDataSize() > 3)
			{
				ev.sysex.resize(message.getRawDataSize());
				memcpy(ev.sysex.data(), message.getRawData(), ev.sysex.size());

				// Juce bug? Or VSTHost bug? Juce inserts f0/f7 when converting VST3 midi packet to Juce packet, but it's already there
				if(ev.sysex.size() > 1)
				{
					if(ev.sysex.front() == 0xf0 && ev.sysex[1] == 0xf0)
						ev.sysex.erase(ev.sysex.begin());

					if(ev.sysex.size() > 1)
					{
						if(ev.sysex[ev.sysex.size()-1] == 0xf7 && ev.sysex[ev.sysex.size()-2] == 0xf7)
							ev.sysex.erase(ev.sysex.begin());
					}
				}
			}
			else
			{
				ev.a = message.getRawData()[0];
				ev.b = message.getRawDataSize() > 0 ? message.getRawData()[1] : 0;
				ev.c = message.getRawDataSize() > 1 ? message.getRawData()[2] : 0;

				const auto status = ev.a & 0xf0;

				if(status == synthLib::M_CONTROLCHANGE || status == synthLib::M_POLYPRESSURE)
				{
					// forward to UI to react to control input changes that should move knobs
					getController().enqueueMidiMessages({ev});
				}
			}

			ev.offset = metadata.samplePosition;

			getPlugin().addMidiEvent(ev);
		}

		midiMessages.clear();

		bool isPlaying = true;
		float bpm = 0.0f;
		float ppqPos = 0.0f;

	    if(const auto* playHead = getPlayHead())
		{
			if(auto pos = playHead->getPosition())
			{
				isPlaying = pos->getIsPlaying();

				if(pos->getBpm())
				{
					bpm = static_cast<float>(*pos->getBpm());
					processBpm(bpm);
				}
				if(pos->getPpqPosition())
				{
					ppqPos = static_cast<float>(*pos->getPpqPosition());
				}
			}
		}

		getPlugin().process(inputs, outputs, numSamples, bpm, ppqPos, isPlaying);

		applyOutputGain(outputs, numSamples);

		m_midiOut.clear();
		getPlugin().getMidiOut(m_midiOut);

	    if (!m_midiOut.empty())
		{
			getController().enqueueMidiMessages(m_midiOut);

		    for (auto& e : m_midiOut)
		    {
			    if (e.source == synthLib::MidiEventSource::Editor || e.source == synthLib::MidiEventSource::Internal)
					continue;

				auto toJuceMidiMessage = [&e]()
				{
					if(!e.sysex.empty())
					{
						assert(e.sysex.front() == 0xf0);
						assert(e.sysex.back() == 0xf7);

						return juce::MidiMessage(e.sysex.data(), static_cast<int>(e.sysex.size()), 0.0);
					}
					const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(e.a);
					if(len == 1)
						return juce::MidiMessage(e.a, 0.0);
					if(len == 2)
						return juce::MidiMessage(e.a, e.b, 0.0);
					return juce::MidiMessage(e.a, e.b, e.c, 0.0);
				};

				const juce::MidiMessage message = toJuceMidiMessage();
				midiMessages.addEvent(message, 0);

				// additionally send to the midi output we've selected in the editor
				if (auto* out = m_midiPorts.getMidiOutput())
					out->sendMessageNow(message);
		    }
		}
	}

	void Processor::processBlockBypassed(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midiMessages)
	{
		if(getProperties().isSynth || getTotalNumInputChannels() <= 0)
		{
			_buffer.clear(0, _buffer.getNumSamples());
			return;
		}

		const auto sampleCount = static_cast<uint32_t>(_buffer.getNumSamples());
		const auto outCount = static_cast<uint32_t>(getTotalNumOutputChannels());
		const auto inCount = static_cast<uint32_t>(getTotalNumInputChannels());

		uint32_t inCh = 0;

		for(uint32_t outCh=0; outCh<outCount; ++outCh)
		{
			auto* input = _buffer.getReadPointer(static_cast<int>(inCh));
			auto* output = _buffer.getWritePointer(static_cast<int>(outCh));

			m_bypassBuffer.write(input, outCh, sampleCount, getLatencySamples());
			m_bypassBuffer.read(output, outCh, sampleCount);

			++inCh;

			if(inCh >= inCount)
				inCh = 0;
		}

//		AudioProcessor::processBlockBypassed(_buffer, _midiMessages);
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

				if (version > g_saveVersion)
					return;

				std::vector<uint8_t> buffer;

				if(version == 1)
				{
					ss.read(buffer);
					getPlugin().setState(buffer);
				}

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

	bool Processor::rebootDevice()
	{
		try
		{
			synthLib::Device* device = createDevice();
			getPlugin().setDevice(device);
			(void)m_device.release();
			m_device.reset(device);

			return true;
		}
		catch(const synthLib::DeviceException& e)
		{
			juce::NativeMessageBox::showMessageBox(juce::MessageBoxIconType::WarningIcon,
				"Device creation failed:",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n");
			return false;
		}
	}
}
