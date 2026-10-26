#include "processor.h"

#include <chrono>
#include <limits>
#include <thread>

#include <juce_audio_formats/juce_audio_formats.h>

#include "dummydevice.h"
#include "midiLearnManager.h"
#include "pluginVersion.h"
#include "tools.h"
#include "types.h"

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"

#include "bridgeLib/commands.h"

#include "client/remoteDevice.h"

#include "synthLib/deviceException.h"
#include "synthLib/os.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiToSysex.h"
#include "synthLib/romLoader.h"
#include "synthLib/wavWriter.h"

#include "dsp56kBase/fastmath.h"
#include "dsp56kBase/logging.h"

#include "juceUiLib/messageBox.h"

namespace synthLib
{
	class DeviceException;
}

namespace pluginLib
{
	constexpr char g_saveMagic[] = "DSP56300";
	constexpr uint32_t g_saveVersion = 2;
	constexpr const char* const g_defaultProgramName = "default";

	bridgeLib::SessionId generateRemoteSessionId()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	Processor::Processor(const BusesProperties& _busesProperties, Properties _properties)
		: juce::AudioProcessor(_busesProperties)
		, m_properties(std::move(_properties))
		, m_midiPorts(*this)
		, m_remoteSessionId(generateRemoteSessionId())
		, m_programName(g_defaultProgramName)
	{
		juce::File(getPublicRomFolder()).createDirectory();

		synthLib::RomLoader::addSearchPath(getPublicRomFolder());
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(true));
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(false));
	}

	Processor::~Processor()
	{
		shutdown();
		destroyController();
		m_plugin.reset();
		m_device.reset();
	}

	void Processor::shutdown()
	{
		if (m_shutdown.exchange(true))
			return;

		// a JUCE MIDI input thread would otherwise keep calling in
		m_midiPorts.close();

		// whoever got past the flag before it was raised is still running, the teardown that follows must wait for it
		while (m_midiInputCalls.load() != 0)
			std::this_thread::yield();

		m_alive.reset();
	}

	void Processor::addMidiEvent(const synthLib::SMidiEvent& _ev)
	{
		// Both counter and flag are sequentially consistent: either shutdown() sees this call, or this call sees the flag
		m_midiInputCalls.fetch_add(1);
		struct Leave
		{
			std::atomic<uint32_t>& calls;
			~Leave() { calls.fetch_sub(1); }
		} leave{m_midiInputCalls};

		if (m_shutdown)
			return;

		audioCaptureCheckArm(_ev);

		// before midi learn or the program change router get a chance to swallow the event: a skin
		// drawing a keyboard wants to see what was played either way
		m_midiNotifier.onMidiEvent(_ev);

		auto toDevice = m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Device);

		// Process through MIDI Learn translator first
		if (_ev.source != synthLib::MidiEventSource::Device)
		{
			if (m_midiLearnTranslator && m_midiLearnTranslator->processMidiInput(_ev))
			{
				// MIDI event was consumed by MIDI Learn (learned mapping or learning mode)
				return;
			}

			if (toDevice)
			{
				std::vector<synthLib::SMidiEvent> replacement;

				switch (m_programChangeRouter.processMidiEvent(_ev, replacement))
				{
				case ProgramChangeRouter::Result::Forward:
					break;
				case ProgramChangeRouter::Result::Held:
					toDevice = false;
					break;
				case ProgramChangeRouter::Result::Consumed:
					// a program change that loads a patch from the patch manager
					for (const auto& ev : replacement)
						getPlugin().addMidiEvent(ev);
					return;
				}
			}
		}

		if (m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Editor))
			getController().enqueueMidiMessages({_ev});
		if (toDevice)
			getPlugin().addMidiEvent(_ev);
		if (m_midiRoutingMatrix.enabled(_ev, synthLib::MidiEventSource::Physical))
			m_midiPorts.send(_ev);
	}

	void Processor::handleIncomingMidiMessage(juce::MidiInput *_source, const juce::MidiMessage &_message)
	{
		synthLib::SMidiEvent sm(synthLib::MidiEventSource::Physical);

		const auto* raw = _message.getSysExData();
		if (raw)
		{
			const auto count = _message.getSysExDataSize();
			auto syx = SysEx();
			syx.push_back(0xf0);
			for (int i = 0; i < count; i++)
			{
				syx.push_back(raw[i]);
			}
			syx.push_back(0xf7);
			sm.sysex = std::move(syx);
		}
		else
		{
			const auto count = _message.getRawDataSize();
			const auto* rawData = _message.getRawData();
			if (count >= 1 && count <= 3)
			{
				synthLib::setShortMessage(sm, rawData, static_cast<size_t>(count));
			}
			else
			{
				auto syx = SysEx();
				for (int i = 0; i < count; i++)
					syx.push_back(rawData[i]);
				sm.sysex = syx;
			}
		}

		addMidiEvent(sm);
	}

	Controller& Processor::getController()
	{
	    if (m_controller == nullptr)
		{
	        m_controller.reset(createController());
			
			// Initialize MIDI Learn translator with controller
			if (m_controller && !m_midiLearnTranslator)
			{
				m_midiLearnTranslator = std::make_unique<MidiLearnTranslator>(*m_controller, m_controller->getParameterDescriptions().getControllerMap());
				
				// Setup MIDI feedback callback
				m_midiLearnTranslator->setOnSendMidiOutput([this](const synthLib::MidiEventSource _target, const synthLib::SMidiEvent& _event)
				{
					if (_target == synthLib::MidiEventSource::Editor && _event.source != synthLib::MidiEventSource::Editor)
						getController().enqueueMidiMessages({_event});
					else if (_target == synthLib::MidiEventSource::Physical && _event.source != synthLib::MidiEventSource::Physical)
						m_midiPorts.send(_event);
					else if (_target == synthLib::MidiEventSource::Host && _event.source != synthLib::MidiEventSource::Host)
						addHostMidiFeedback(_event);
				});

				// Load default MIDI learn preset from disk. DAW state restore
				// (setStateInformation) will override this if present.
				loadDefaultMidiLearnPreset();
			}

			// the controller reads the state of the device once, here instead of in each product's constructor
			if (m_controller)
				m_controller->onStateLoaded();
		}

	    return *m_controller;
	}

	synthLib::Plugin& Processor::getPlugin()
	{
		// Serialize lazy device/plugin creation (see m_deviceCreateMutex). In steady state this is
		// uncontended and the m_plugin check below returns immediately; only the very first call holds it
		// for the whole boot, during which any concurrent caller correctly waits instead of creating a
		// second device on top of the one still booting.
		std::lock_guard<std::mutex> lock(m_deviceCreateMutex);

		if(m_plugin)
			return *m_plugin;

		try
		{
			m_device.reset(createDevice());
			m_deviceVoiceExpansion = m_voiceExpansion;
			if(!m_device->isValid())
				throw synthLib::DeviceException(synthLib::DeviceError::Unknown, "Device initialization failed");
		}
		catch(const synthLib::DeviceException& e)
		{
			LOG("Failed to create device: " << e.what());

			// Juce loads the LV2/VST3 versions of the plugin as part of the build process, if we open a message box in this case, the build process gets stuck
			const auto host = juce::PluginHostType::getHostPath();
			if(!genericUI::MessageBox::isHeadlessHost())
			{
				std::string msg = e.what();

				m_deviceError = e.errorCode();

				if(e.errorCode() == synthLib::DeviceError::FirmwareMissing)
				{
					msg += "\n\n";
					msg += "The firmware file needs to be copied to\n";
					msg += baseLib::filesystem::validatePath(getPublicRomFolder()) + "\n";
					msg += "\n";
					msg += "The target folder will be opened once you click OK. Copy the firmware to this folder, then click \"Reboot Device\" in the MIDI settings or reload the plugin.";
#ifdef _DEBUG
					msg += "\n\n" + std::string("[Debug] Host ") + host.toStdString() + "\n\n";
#endif
				}
				juce::Timer::callAfterDelay(2000, [alive = std::weak_ptr(m_alive), romFolder = getPublicRomFolder(), msg]
				{
					if (alive.expired())
						return;
					genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
						"Device Initialization failed", msg, 
						[romFolder]
						{
							const auto path = juce::File(romFolder);
							(void)path.createDirectory();
							path.revealToUser();
						}
					);
				});
			}
		}

		if(!m_device)
		{
			m_device.reset(new DummyDevice({}));
		}

		m_device->setDspClockPercent(m_dspClockPercent);

		m_plugin.reset(new synthLib::Plugin(m_device.get(), [this](synthLib::Device* _device)
		{
			onDeviceInvalid(_device);
		}));

		m_plugin->setResamplerMode(m_resamplerMode);

		return *m_plugin;
	}

	bridgeClient::RemoteDevice* Processor::createRemoteDevice(const synthLib::DeviceCreateParams& _params)
	{
		bridgeLib::PluginDesc desc;
		getPluginDesc(desc);
		return new bridgeClient::RemoteDevice(_params, std::move(desc), m_remoteHost, m_remotePort);
	}

	void Processor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		_params.preferredSamplerate = getPreferredDeviceSamplerate();
		_params.hostSamplerate = getHostSamplerate();

		if (m_voiceExpansion)
			_params.customData |= 1;
	}

	bridgeClient::RemoteDevice* Processor::createRemoteDevice()
	{
		synthLib::DeviceCreateParams params;
		getRemoteDeviceParams(params);
		return createRemoteDevice(params);
	}

	synthLib::Device* Processor::createDevice()
	{
		// a product that has a better text for a missing ROM throws it from getRemoteDeviceParams
		synthLib::DeviceCreateParams params;
		getRemoteDeviceParams(params);
		params.homePath = getDataFolder();

		auto* device = newDevice(params);

		if(!device->isValid())
		{
			delete device;
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, getProperties().name + ": no valid firmware ROM found");
		}
		return device;
	}

	synthLib::Device* Processor::createDevice(const DeviceType _type)
	{
		switch (_type)
		{
		case DeviceType::Local:		return createDevice();
		case DeviceType::Remote:	return createRemoteDevice();
		case DeviceType::Dummy:		return new DummyDevice({});
		}
		return nullptr;
	}

	bool Processor::setLatencyBlocks(uint32_t _blocks)
	{
		if (!getPlugin().setLatencyBlocks(_blocks))
			return false;
		updateLatencySamples();
		return true;
	}

	uint32_t Processor::getCurrentLatency()
	{
		return getProperties().isSynth ? getPlugin().getLatencyMidiToOutput() : getPlugin().getLatencyInputToOutput();
	}

	void Processor::updateLatencySamples()
	{
		m_reportedLatency = getCurrentLatency();
		setLatencySamples(static_cast<int>(m_reportedLatency));
	}

	void Processor::handleAsyncUpdate()
	{
		// Order matters: switching the resampler moves the latency, so publish it afterwards.
		getPlugin().applyPendingDeviceSamplerate();
		updateLatencySamples();
	}

	void Processor::saveCustomData(std::vector<uint8_t>& _targetBuffer)
	{
		baseLib::BinaryStream s;
		saveChunkData(s);
		s.toVector(_targetBuffer, true);
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

		// the chunk is only written while the voice expansion is on, a state without it had it off
		const auto voiceExpansion = m_voiceExpansion;
		m_voiceExpansion = false;

		const auto result = _sourceBuffer.empty() || (cr.tryRead() && cr.numRead() > 0);

		if (!result)
			m_voiceExpansion = voiceExpansion;

		// Compare with the device, not with the setting before the load: REMO creates a remote device while reading,
		// with the setting read so far, also when a later chunk fails. Without a device yet, the one created later gets
		// the setting anyway
		if (m_plugin && m_voiceExpansion != m_deviceVoiceExpansion)
			rebootDevice();

		return result;
	}

	void Processor::saveChunkData(baseLib::BinaryStream& s)
	{
		// before REMO: reading REMO creates the remote device, which has to get the voice expansion right away
		if (m_voiceExpansion)
		{
			baseLib::ChunkWriter cw(s, "VEXP", 1);
			s.write<uint8_t>(1);
		}

		// after VEXP, which only sets the flag, and before REMO: reading it creates the device of that firmware, which
		// gets the voice expansion right away and is the one REMO and the device state go to
		if (m_firmwareIndex < m_firmwareNames.size())
		{
			baseLib::ChunkWriter cw(s, m_firmwareChunk.id, m_firmwareChunk.version);
			m_firmwareChunk.write(s, m_firmwareIndex);
		}

		// it is important that this is stored before other chunks to restore state to the remote properly
		if (m_deviceType == DeviceType::Remote)
		{
			baseLib::ChunkWriter cw(s, "REMO", 1);
			s.write(static_cast<int32_t>(m_deviceType));
			s.write(m_remoteHost);
			s.write(m_remotePort);
		}

		{
			std::vector<uint8_t> buffer;
			getPlugin().getState(buffer, synthLib::StateTypeGlobal);

			baseLib::ChunkWriter cw(s, "MIDI", 1);
			s.write(buffer);
		}
		{
			baseLib::ChunkWriter cw(s, "GAIN", 1);
			s.write<uint32_t>(1);	// version
			s.write(m_inputGain.load());
			s.write(m_outputGain.load());
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

		{
			// Always written, including Legacy. The default is Mame HQ now, so a state that omits the
			// chunk cannot be read as "Legacy was chosen" any more - it means the state predates this
			// and the setting falls back to the one the user last picked. (BUG-10273, BUG-10277)
			baseLib::ChunkWriter cw(s, "RSMP", 1);
			s.write(static_cast<uint8_t>(m_resamplerMode));
		}

		m_midiPorts.saveChunkData(s);
		m_skinVariables.saveChunkData(s);
		m_midiRoutingMatrix.saveChunkData(s);

		if (m_midiLearnTranslator)
			m_midiLearnTranslator->saveChunkData(s);

		if (m_programName != g_defaultProgramName)
		{
			baseLib::ChunkWriter cw(s, "PROG", 1);
			s.write(m_programName);
		}
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

		_cr.add("RSMP", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			const auto mode = _binaryStream.read<uint8_t>();
			if(mode < static_cast<uint8_t>(synthLib::Resampler::Mode::Count))
				setResamplerMode(static_cast<synthLib::Resampler::Mode>(mode));
		});

		_cr.add("PROG", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			m_programName = _binaryStream.readString();
		});

		_cr.add("VEXP", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			m_voiceExpansion = _binaryStream.read<uint8_t>() != 0;
		});

		// a state that names an image that is not there keeps the running one
		if (m_firmwareChunk.find)
		{
			_cr.add(m_firmwareChunk.id, m_firmwareChunk.version, [this](baseLib::BinaryStream& _binaryStream, const uint32_t _version)
			{
				if (const auto index = m_firmwareChunk.find(_binaryStream, _version))
					selectFirmware(*index);
			});
		}

		_cr.add("REMO", 1, [this](baseLib::BinaryStream& _binaryStream, uint32_t _version)
		{
			const auto type = static_cast<DeviceType>(_binaryStream.read<int32_t>());
			const auto host = _binaryStream.readString();
			const auto port = _binaryStream.read<uint32_t>();
			if (type == DeviceType::Remote)
				setRemoteDevice(host, port);
		});

		m_midiPorts.loadChunkData(_cr);
		m_skinVariables.loadChunkData(_cr);
		m_midiRoutingMatrix.loadChunkData(_cr);
		
		if (m_midiLearnTranslator)
			m_midiLearnTranslator->loadChunkData(_cr);
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
		// under the processing lock: a remote device's request must not overlap the audio thread's, and a local
		// one must not change its clock in the middle of a block
		if(!getPlugin().setDspClockPercent(_percent))
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

	bool Processor::canModifyDspClock() const
	{
		if(!m_device)
			return false;
		return m_device->canModifyDspClock();
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
		std::vector<float> result;
		m_device->getSupportedSamplerates(result);
		return result;
	}

	std::vector<float> Processor::getDevicePreferredSamplerates() const
	{
		if(!m_device)
			return {};
		std::vector<float> result;
		m_device->getPreferredSamplerates(result);
		return result;
	}

	void Processor::setResamplerMode(const synthLib::Resampler::Mode _mode)
	{
		m_resamplerMode = _mode;

		// Do not reach for getPlugin() here: this is set from the config before anything else has
		// touched the plugin, and booting a device from there would cost seconds at construction.
		// A plugin created later picks the mode up in getPlugin().
		if (m_plugin)
			m_plugin->setResamplerMode(_mode);
	}

	std::optional<std::pair<const char*, uint32_t>> Processor::findResource(const BinaryDataRef& _binaryData,	const std::string& _filename)
	{
		for(uint32_t i=0; i<_binaryData.listSize; ++i)
		{
			if (_binaryData.originalFileNames[i] != _filename)
				continue;

			int size = 0;
			const auto res = _binaryData.getNamedResourceFunc(_binaryData.namedResourceList[i], size);
			return {std::make_pair(res, static_cast<uint32_t>(size))};
		}
		return {};
	}

	std::optional<std::pair<const char*, uint32_t>> Processor::findResource(const std::string& _filename) const
	{
		return findResource(m_properties.binaryData, _filename);
	}

	std::string Processor::getDataFolder(const bool _useFxFolder) const
	{
		return Tools::getPublicDataFolder(getProperties().vendor, getProductName(_useFxFolder));
	}

	std::string Processor::getPublicRomFolder() const
	{
		return baseLib::filesystem::validatePath(getDataFolder() + "roms/");
	}

	std::string Processor::getConfigFolder(const bool _useFxFolder) const
	{
		return baseLib::filesystem::validatePath(getDataFolder(_useFxFolder) + "config/");
	}

	std::string Processor::getPatchManagerDataFolder(bool _useFxFolder) const
	{
		return baseLib::filesystem::validatePath(getDataFolder(_useFxFolder) + "patchmanager/");
	}

	std::string Processor::getConfigFile(const bool _useFxFolder) const
	{
		return getConfigFolder(_useFxFolder) + getProductName(_useFxFolder) + ".xml";
	}

	std::string Processor::getProductName(const bool _useFxName) const
	{
		const auto& p = getProperties();
		auto name = p.name;
		if(!_useFxName && !p.isSynth && name.substr(name.size()-2, 2) == "FX")
			return name.substr(0, name.size() - 2);
		return name;
	}

	void Processor::saveDefaultMidiLearnPreset()
	{
		if (!m_midiLearnTranslator)
			return;

		MidiLearnManager manager{juce::File(getMidiLearnFolder())};
		manager.savePreset("__default", m_midiLearnTranslator->getPreset());
	}

	void Processor::loadDefaultMidiLearnPreset()
	{
		if (!m_midiLearnTranslator)
			return;

		MidiLearnManager manager{juce::File(getMidiLearnFolder())};
		MidiLearnPreset preset;

		if (manager.loadPreset("__default", preset))
			m_midiLearnTranslator->setPreset(preset);
	}

	std::string Processor::getMidiLearnFolder() const
	{
		return getConfigFolder() + "midilearn";
	}

	void Processor::getPluginDesc(bridgeLib::PluginDesc& _desc) const
	{
		_desc.plugin4CC = getProperties().plugin4CC;
		_desc.pluginName = getProperties().name;
		_desc.pluginVersion = Version::getVersionNumber();
		_desc.sessionId = m_remoteSessionId;
	}

	void Processor::setDeviceType(const DeviceType _type, const bool _forceChange/* = false*/)
	{
		if(m_deviceType == _type && !_forceChange)
			return;

		try
		{
			if(auto* dev = createDevice(_type))
			{
				getPlugin().setDevice(dev);
				(void)m_device.release();
				m_device.reset(dev);
				m_deviceVoiceExpansion = m_voiceExpansion;
				m_deviceType = _type;
			}
		}
		catch(synthLib::DeviceException& e)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				getName().toStdString() + " - Failed to switch device type",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n");
		}

		if(_type != DeviceType::Remote)
			m_remoteSessionId = generateRemoteSessionId();
	}

	void Processor::setRemoteDevice(const std::string& _host, const uint32_t _port)
	{
		if(m_remotePort == _port && m_remoteHost == _host && m_deviceType == DeviceType::Remote)
			return;

		m_remoteHost = _host;
		m_remotePort = _port;
		setDeviceType(DeviceType::Remote, true);
	}

	void Processor::destroyController()
	{
		m_midiLearnTranslator.reset();
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

		// JUCE reports the buses it constructs before this class is there to hear it
		updateOutputMap();
		m_outputScratch.setSize(static_cast<int>(synthLib::TAudioOutputs().size()), samplesPerBlock);

		// (Re)allocate the audio-capture buffer for this sample rate, hard-capped so a capture that is never
		// stopped cannot grow unbounded. Done here (no audio running) so the audio thread never allocates.
		{
			constexpr double captureMaxSeconds = 30.0;
			m_captureState.store(CaptureState::Idle, std::memory_order_release);
			m_capturePos.store(0, std::memory_order_relaxed);
			m_captureMaxFrames.store(0, std::memory_order_relaxed);
			m_captureBuffer.setSize(g_captureChannels, static_cast<int>(captureMaxSeconds * sampleRate), false, true, true);
			m_captureBuffer.clear();
		}
	}

	void Processor::processorLayoutsChanged()
	{
		updateOutputMap();
	}

	void Processor::updateOutputMap()
	{
		std::vector<uint32_t> map;
		bool inOrder = true;
		uint32_t paired = 0;	// the outputs of the buses not declared mono so far
		uint32_t single = 0;	// the buses declared mono so far

		for(int b = 0; b < getBusCount(false); ++b)
		{
			const auto* bus = getBus(false, b);
			if(!bus)
				continue;

			const auto& declared = bus->getDefaultLayout();
			const bool mono = declared == juce::AudioChannelSet::mono();

			for(int c = 0; bus->isEnabled() && c < bus->getNumberOfChannels(); ++c)
			{
				const auto output = mono ? single : paired + static_cast<uint32_t>(c);
				inOrder &= output == map.size();
				map.push_back(output);
			}

			if(mono)
				++single;
			else
				paired += static_cast<uint32_t>(declared.size());
		}

		if(inOrder)
			map.clear();
		m_outputMap = std::move(map);
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
		PluginStream ss;
		ss.write(g_saveMagic);
		ss.write(g_saveVersion);
		std::vector<uint8_t> buffer;
		saveCustomData(buffer);
		ss.write(buffer);

		std::vector<uint8_t> buf;
		ss.toVector(buf);

		destData.append(buf.data(), buf.size());
	}

	void Processor::setStateInformation (const void* _data, const int _sizeInBytes)
	{
		// You should use this method to restore your parameters from this memory block,
	    // whose contents will have been created by the getStateInformation() call.
		setState(_data, _sizeInBytes);
	}

	void Processor::getCurrentProgramStateInformation(juce::MemoryBlock& destData)
	{
		std::vector<uint8_t> state;
		getPlugin().getState(state, synthLib::StateTypeCurrentProgram);
		destData.append(state.data(), state.size());
	}

	void Processor::setCurrentProgramStateInformation(const void* data, int sizeInBytes)
	{
		setState(data, sizeInBytes);
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

		// The buffer can have fewer channels than the buses: a standalone that cannot match the bus layout of a product
		// plays it with the channels of the audio device (BUG-10518). The plugin plays the inputs missing here as silence
		// and writes the outputs missing here into a dummy buffer
	    const auto totalNumInputChannels  = std::min(getTotalNumInputChannels(), buffer.getNumChannels());
	    const auto totalNumOutputChannels = std::min(getTotalNumOutputChannels(), buffer.getNumChannels());

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

		injectTestInput(buffer, totalNumInputChannels, numSamples);

		for (int channel = 0; channel < totalNumInputChannels; ++channel)
			inputs[channel] = buffer.getReadPointer(channel);

		// With an output map, see updateOutputMap, the device renders into a buffer of its own and each channel gets a copy
		// of its output below. Rendered in place, an output could overwrite another input the device still reads
		const auto mapped = !m_outputMap.empty();
		if(mapped)
		{
			m_outputScratch.setSize(static_cast<int>(outputs.size()), numSamples, false, false, true);
			for(const auto o : m_outputMap)
			{
				if(o < outputs.size())
					outputs[o] = m_outputScratch.getWritePointer(static_cast<int>(o));
			}
		}
		else
		{
			for (int channel = 0; channel < totalNumOutputChannels; ++channel)
				outputs[channel] = buffer.getWritePointer(channel);
		}

		m_programChangeRouter.releaseStaleHold([this](const synthLib::SMidiEvent& _ev) { getPlugin().addMidiEvent(_ev); });

		for(const auto metadata : midiMessages)
		{
			const auto message = metadata.getMessage();

			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);

			if(message.isSysEx() || message.getRawDataSize() > 3)
			{
				ev.sysex.resize(message.getRawDataSize());
				memcpy(ev.sysex.data(), message.getRawData(), ev.sysex.size());

				// guards against hosts that hand over sysex framed twice (f0 f0 ... f7 f7)
				synthLib::MidiToSysex::removeDuplicateFraming(ev.sysex);
			}
			else
			{
				synthLib::setShortMessage(ev, message.getRawData(), static_cast<size_t>(message.getRawDataSize()));
			}

			ev.offset = std::max(0, metadata.samplePosition);

			addMidiEvent(ev);
		}

		midiMessages.clear();

		bool isPlaying = true;
		bool hasPpqPosition = false;
		float bpm = 0.0f;
		double ppqPos = 0.0;
		synthLib::HostLoop loop;

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
					ppqPos = *pos->getPpqPosition();
					hasPpqPosition = true;
				}
				if(const auto points = pos->getLoopPoints())
				{
					loop.active = pos->getIsLooping();
					loop.ppqStart = points->ppqStart;
					loop.ppqEnd = points->ppqEnd;
				}
			}
		}

		getPlugin().process(inputs, outputs, numSamples, bpm, ppqPos, isPlaying, hasPpqPosition, loop);

		// Both checks are plain atomic loads, cheap enough per block. The work they stand for
		// belongs to the message thread, see handleAsyncUpdate().
		if(getCurrentLatency() != m_reportedLatency || getPlugin().hasPendingDeviceSamplerate())
			triggerAsyncUpdate();

		applyOutputGain(outputs, numSamples);

		if(mapped)
		{
			for(int channel = 0; channel < totalNumOutputChannels; ++channel)
			{
				const auto o = static_cast<size_t>(channel) < m_outputMap.size() ? m_outputMap[static_cast<size_t>(channel)] : outputs.size();
				if(o < outputs.size())
					buffer.copyFrom(channel, 0, outputs[o], numSamples);
				else
					buffer.clear(channel, 0, numSamples);
			}
		}

		captureAudioBlock(buffer, numSamples);
		m_outputLevels.process(buffer, getTotalNumOutputChannels(), numSamples, getSampleRate());

		m_midiOut.clear();
		getPlugin().getMidiOut(m_midiOut);

	    for (auto& e : m_midiOut)
	    {
		    addMidiEvent(e);

			if (!getMidiRoutingMatrix().enabled(e, synthLib::MidiEventSource::Host))
			    continue;

	    	const auto mm = MidiPorts::toJuceMidiMessage(e);
		    midiMessages.addEvent(mm, 0);
	    }

		// Drain MIDI Learn feedback events destined for the host
		{
			const std::scoped_lock lock(m_hostFeedbackMutex);
			for (const auto& e : m_hostFeedbackQueue)
			{
				const auto mm = MidiPorts::toJuceMidiMessage(e);
				midiMessages.addEvent(mm, 0);
			}
			m_hostFeedbackQueue.clear();
		}
	}

	namespace
	{
		// Interleave the float capture to 16-bit PCM (clamped to [-1,1]) and write it with the shared
		// synthLib::WavWriter rather than hand-rolling a RIFF header.
		void writeWav16(const std::string& _path, const juce::AudioBuffer<float>& _buffer, const int _frames, const int _channels, const double _sampleRate)
		{
			std::vector<int16_t> pcm;
			pcm.reserve(static_cast<size_t>(_frames) * static_cast<size_t>(_channels));
			for(int i = 0; i < _frames; ++i)
			{
				for(int ch = 0; ch < _channels; ++ch)
				{
					const float s = std::max(-1.0f, std::min(1.0f, _buffer.getReadPointer(ch)[i]));
					pcm.push_back(static_cast<int16_t>(s * 32767.0f));
				}
			}
			synthLib::WavWriter().write(_path, 16, false, _channels, static_cast<int>(_sampleRate), pcm);
		}
	}

	void Processor::audioCaptureCheckArm(const synthLib::SMidiEvent& _ev)
	{
		// A played note-on (not a device echo) starts a pending (armed) capture.
		if(m_captureState.load(std::memory_order_acquire) != CaptureState::Armed)
			return;
		if(_ev.source == synthLib::MidiEventSource::Device)
			return;
		if((_ev.a & 0xf0) != synthLib::M_NOTEON || _ev.c == 0)
			return;
		m_captureStarted.store(true, std::memory_order_relaxed);
		m_captureState.store(CaptureState::Recording, std::memory_order_release);
	}

	void Processor::captureAudioBlock(const juce::AudioBuffer<float>& _buffer, const int _numSamples)
	{
		if(m_captureState.load(std::memory_order_acquire) != CaptureState::Recording)
			return;

		const int capacity = m_captureBuffer.getNumSamples();
		const int limit = std::min(m_captureMaxFrames.load(std::memory_order_relaxed), capacity);
		const int pos = m_capturePos.load(std::memory_order_relaxed);
		const int room = limit - pos;
		if(room <= 0)
		{
			m_captureState.store(CaptureState::Done, std::memory_order_release);
			return;
		}

		const int n = std::min(_numSamples, room);
		const int srcChannels = _buffer.getNumChannels();
		for(int ch = 0; ch < m_captureBuffer.getNumChannels(); ++ch)
		{
			if(srcChannels <= 0)
			{
				m_captureBuffer.clear(ch, pos, n);
				continue;
			}
			const int srcCh = ch < srcChannels ? ch : srcChannels - 1;
			m_captureBuffer.copyFrom(ch, pos, _buffer, srcCh, 0, n);
		}

		// Publish pos only after the samples are written, so a concurrent stopAudioCapture reads a count whose
		// data is fully present.
		m_capturePos.store(pos + n, std::memory_order_release);
		if(pos + n >= limit)
			m_captureState.store(CaptureState::Done, std::memory_order_release);
	}

	void Processor::startAudioCapture(const uint32_t _maxFrames, const bool _armOnNote)
	{
		// Halt any in-flight capture first; the audio thread stops touching the buffer once state != Recording.
		m_captureState.store(CaptureState::Idle, std::memory_order_release);
		const int capacity = m_captureBuffer.getNumSamples();
		const int frames = _maxFrames == 0 ? capacity : std::min(static_cast<int>(_maxFrames), capacity);
		m_captureMaxFrames.store(frames, std::memory_order_relaxed);
		m_captureStarted.store(!_armOnNote, std::memory_order_relaxed);
		m_capturePos.store(0, std::memory_order_release);
		m_captureState.store(_armOnNote ? CaptureState::Armed : CaptureState::Recording, std::memory_order_release);
	}

	bool Processor::isAudioCaptureActive() const
	{
		const auto s = m_captureState.load(std::memory_order_acquire);
		return s == CaptureState::Armed || s == CaptureState::Recording;
	}

	Processor::AudioCaptureResult Processor::stopAudioCapture(const std::string& _wavPath)
	{
		// Stop the audio thread from writing further, then read what was captured (the region [0, pos) is stable
		// once we observe pos, since the audio thread only ever appends).
		m_captureState.store(CaptureState::Done, std::memory_order_release);

		AudioCaptureResult r;
		const int channels = m_captureBuffer.getNumChannels();
		const int frames = std::max(0, std::min(m_capturePos.load(std::memory_order_acquire), m_captureBuffer.getNumSamples()));
		r.valid = true;
		r.started = m_captureStarted.load(std::memory_order_relaxed);
		r.frames = static_cast<uint32_t>(frames);
		r.channels = static_cast<uint32_t>(channels);
		r.sampleRate = getSampleRate();

		double sumSquares = 0.0;
		float peak = 0.0f;
		for(int ch = 0; ch < channels; ++ch)
		{
			const float* data = m_captureBuffer.getReadPointer(ch);
			for(int i = 0; i < frames; ++i)
			{
				const float a = std::fabs(data[i]);
				if(a > peak)
					peak = a;
				sumSquares += static_cast<double>(data[i]) * data[i];
			}
		}
		const double sampleTotal = static_cast<double>(frames) * channels;
		r.peak = peak;
		r.rms = sampleTotal > 0.0 ? static_cast<float>(std::sqrt(sumSquares / sampleTotal)) : 0.0f;

		if(frames > 0 && !_wavPath.empty())
			writeWav16(_wavPath, m_captureBuffer, frames, channels, r.sampleRate);

		m_captureState.store(CaptureState::Idle, std::memory_order_release);
		return r;
	}

	Processor::TestInputResult Processor::startTestInput(const std::string& _path, const bool _loop)
	{
		TestInputResult r;

		juce::AudioFormatManager formats;
		formats.registerBasicFormats();
		const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(_path)));
		if(!reader)
		{
			r.error = "Cannot read " + _path + ", it needs to be a WAV or AIFF file";
			return r;
		}
		if(reader->numChannels == 0 || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max() ||
			reader->sampleRate <= 0.0)
		{
			r.error = "No usable audio in " + _path;
			return r;
		}

		const auto channels = static_cast<int>(reader->numChannels);
		const auto length = static_cast<int>(reader->lengthInSamples);

		// Kept at the rate of the file. The host may not have told its rate yet, so injectTestInput converts
		auto input = std::make_shared<TestInput>();
		input->audio.setSize(channels, length);
		reader->read(&input->audio, 0, length, 0, true, true);
		input->sampleRate = reader->sampleRate;
		input->loop = _loop;
		input->resamplers.resize(static_cast<size_t>(channels));

		r.valid = true;
		r.frames = static_cast<uint32_t>(length);
		r.channels = static_cast<uint32_t>(channels);
		r.fileSampleRate = reader->sampleRate;

		m_testInputPrev = std::atomic_exchange(&m_testInput, std::move(input));
		return r;
	}

	bool Processor::stopTestInput()
	{
		m_testInputPrev = std::atomic_exchange(&m_testInput, std::shared_ptr<TestInput>());
		return m_testInputPrev != nullptr;
	}

	void Processor::injectTestInput(juce::AudioBuffer<float>& _buffer, const int _numChannels, const int _numSamples) const
	{
		const auto input = std::atomic_load(&m_testInput);
		if(!input)
			return;

		const auto& audio = input->audio;
		const int length = audio.getNumSamples();
		const int fileChannels = audio.getNumChannels();
		int pos = input->pos.load(std::memory_order_relaxed);

		// Converted while playing, at the rate the host runs at now, not at the one it had when the file was set
		const double hostRate = getSampleRate();
		if(hostRate > 0.0 && std::abs(input->sampleRate - hostRate) > 0.01)
		{
			const bool playing = pos < length;	// a looping file wraps around inside the resampler
			const double ratio = input->sampleRate / hostRate;	// file samples per host sample
			int used = 0;

			for(int ch = 0; ch < _numChannels; ++ch)
			{
				if(playing && ch < fileChannels)
					used = input->resamplers[ch].process(ratio, audio.getReadPointer(ch, pos), _buffer.getWritePointer(ch),
						_numSamples, length - pos, input->loop ? length : 0);
				else if(playing && fileChannels == 1)
					_buffer.copyFrom(ch, 0, _buffer, 0, 0, _numSamples);
				else
					_buffer.clear(ch, 0, _numSamples);
			}

			// When wrapping around, the resampler returns the samples it used modulo the length
			input->pos.store(input->loop ? (pos + used) % length : pos + used, std::memory_order_relaxed);
			return;
		}

		for(int i = 0; i < _numSamples; ++i)
		{
			if(pos >= length && input->loop)
				pos = 0;
			const bool playing = pos < length;

			for(int ch = 0; ch < _numChannels; ++ch)
			{
				const int src = fileChannels == 1 ? 0 : ch;
				_buffer.setSample(ch, i, playing && src < fileChannels ? audio.getSample(src, pos) : 0.0f);
			}

			if(playing)
				++pos;
		}

		input->pos.store(pos, std::memory_order_relaxed);
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
		return m_programName;
	}

	void Processor::addHostMidiFeedback(const synthLib::SMidiEvent& _event)
	{
		const std::scoped_lock lock(m_hostFeedbackMutex);
		m_hostFeedbackQueue.push_back(_event);
	}

	void Processor::changeProgramName(int _index, const juce::String& _newName)
	{
		m_programName = _newName.toStdString();
	}

	double Processor::getTailLengthSeconds() const
	{
		return 0.0f;
	}

	void Processor::onDeviceInvalid(synthLib::Device* _device)
	{
		// The audio thread must neither wait for a reconnect nor boot a device, and setDevice() takes the locks that
		// process() holds. The device is replaced on the message thread, the plugin outputs silence until then
		juce::MessageManager::callAsync([this, alive = std::weak_ptr(m_alive), _device]
		{
			if (!alive.expired())
				replaceInvalidDevice(_device);
		});
	}

	void Processor::replaceInvalidDevice(const synthLib::Device* _device)
	{
		// replaced already, by a reboot or a device type switch that was queued before
		if (getPlugin().getDevice() != _device)
			return;

		if(dynamic_cast<const bridgeClient::RemoteDevice*>(_device))
		{
			try
			{
				// attempt one reconnect
				auto* newDevice = createRemoteDevice();
				if(newDevice && newDevice->isValid())
				{
					getPlugin().setDevice(newDevice);
					(void)m_device.release();
					m_device.reset(newDevice);
					m_deviceVoiceExpansion = m_voiceExpansion;
					if (hasController())
						getController().onStateLoaded();
					return;
				}
				delete newDevice;
			}
			catch (synthLib::DeviceException& e)
			{
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
					"Device creation failed:",
					std::string("The connection to the remote server has been lost and a reconnect failed. Processing mode has been switched to local processing\n\n") + 
					e.what() + "\n\n");
			}
		}

		setDeviceType(DeviceType::Local, true);

		if (hasController())
			getController().onStateLoaded();
	}

	void Processor::setVoiceExpansion(const bool _enabled)
	{
		if (m_voiceExpansion == _enabled)
			return;
		m_voiceExpansion = _enabled;
		rebootDevice();
	}

	void Processor::setFirmwareImages(std::vector<std::string> _names, FirmwareChunk _chunk)
	{
		m_firmwareNames = std::move(_names);
		m_firmwareChunk = std::move(_chunk);
		m_firmwareIndex = 0;
		evFirmwareChanged.retain(m_firmwareIndex);
	}

	bool Processor::selectFirmware(const uint32_t _index)
	{
		if (_index >= m_firmwareNames.size())
			return false;
		if (_index == m_firmwareIndex)
			return true;

		const auto previous = m_firmwareIndex;
		m_firmwareIndex = _index;

		// without a device yet, the one created later runs this image
		if (m_plugin && !rebootDevice())
		{
			m_firmwareIndex = previous;
			return false;
		}

		evFirmwareChanged.retain(m_firmwareIndex);
		return true;
	}

	bool Processor::rescanFirmwareAndReboot()
	{
		auto names = findNewFirmwareImages();
		m_firmwareNames.insert(m_firmwareNames.end(), std::make_move_iterator(names.begin()), std::make_move_iterator(names.end()));

		const auto rebooted = rebootDevice();

		// a product without a list of images looks its firmware up on each reboot, it may be another one now
		if (!names.empty() || m_firmwareNames.empty())
			evFirmwareChanged.retain(m_firmwareIndex);

		return rebooted;
	}

	bool Processor::rebootDevice()
	{
		// Make sure the plugin (and with it the device it wraps) exists before we
		// build its replacement, otherwise the lazy creation below would boot a
		// device just to throw it away again.
		auto& plugin = getPlugin();

		try
		{
			// Recreate the device type that is actually in use. Creating a local
			// device unconditionally would silently drop a remote (bridged)
			// session while m_deviceType still claims Remote.
			synthLib::Device* device = createDevice(m_deviceType);
			if(!device)
				return false;

			// Carry over what lives on the processor rather than in the device
			// state blob. Latency and samplerate are reapplied by setDevice(),
			// which also transfers the state of the device being replaced.
			device->setDspClockPercent(m_dspClockPercent);

			plugin.setDevice(device);
			(void)m_device.release();
			m_device.reset(device);
			m_deviceVoiceExpansion = m_voiceExpansion;

			// The same resync a DAW restore performs: the editor is talking to a
			// device that just came up fresh, so it has to re-read it.
			if(hasController())
				getController().onStateLoaded();

			return true;
		}
		catch(const synthLib::DeviceException& e)
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
				"Device creation failed:",
				std::string("Failed to create device:\n\n") + 
				e.what() + "\n\n");
			return false;
		}
	}
}
