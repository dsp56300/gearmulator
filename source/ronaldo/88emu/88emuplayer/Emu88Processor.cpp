#include "88emuplayer/Emu88Processor.h"
#include "88emuplayer/app/Emu88LaunchOptions.h"

#include "88emuplayer/ui/Emu88Editor.h"
#include "jucePlayerLib/portMidiBridge.h"

#include "88lib/rom/romloader.h"

#include "baseLib/filesystem.h"
#include "baseLib/md5.h"


namespace emu88Player
{
	namespace
	{
		constexpr auto g_deviceModelKey = "deviceModel";
		constexpr auto g_outputGainKey = "outputGain";
		constexpr auto g_factoryResetOnLoadKey = "factoryResetOnLoad";
		constexpr auto g_fastBootKey = "fastBoot";

	}

	Processor::Processor()
		: juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
		  m_dataFolder(defaultDataFolder()),
		  m_romFolder(standaloneLaunch && standaloneLaunch->has("rom-dir")
			? launchFile(standaloneLaunch->get("rom-dir")).getFullPathName().toStdString()
			: defaultDataFolder() + "roms/"),
		  m_ownedConfig(standaloneConfig ? nullptr : createConfig(m_dataFolder)),
		  m_config(standaloneConfig ? standaloneConfig : m_ownedConfig.get())
	{
		if(!standaloneLaunch || !standaloneLaunch->has("rom-dir"))
			(void)juce::File(m_romFolder).createDirectory();
		configureRomSearchPaths(standaloneLaunch ? *standaloneLaunch : LaunchOptions{});
		// Live MIDI can arrive before the audio device starts; prepareToPlay() sets the real rate.
		for(auto& collector : m_liveMidi)
			collector.reset(44100.0);

		const auto configuredModel = m_config->getIntValue(g_deviceModelKey,
			static_cast<int>(emu88Lib::DeviceModel::Sc88Pro));
		if(configuredModel >= 0 && emu88Lib::isDeviceModelValue(static_cast<uint32_t>(configuredModel)))
			m_deviceModel = static_cast<emu88Lib::DeviceModel>(configuredModel);

		// A board is only selectable once every ROM the registry lists for it is
		// present. If the configured one is not, start on the first that is
		// rather than booting into a dead device - the configured choice is left
		// in the config so it comes back once its ROMs turn up.
		if(!(standaloneLaunch && standaloneLaunch->has("device")) && !isModelAvailable(m_deviceModel))
		{
			if(const auto fallback = firstAvailableModel())
				m_deviceModel = *fallback;
		}

		m_midiPlayer.setResetMode(static_cast<jucePlayer::MidiPlayer::ResetMode>(
			m_config->getIntValue("songResetMode", static_cast<int>(jucePlayer::MidiPlayer::ResetMode::Gs))));
		m_midiPlayer.setSongGapMs(static_cast<uint32_t>(std::max(0, m_config->getIntValue("songGapMs", 1000))));
		m_midiPlayer.setPortCount(midiPortCount());

		const auto analogMode = m_config->getIntValue("analogOutputMode",
			static_cast<int>(emu88Lib::AnalogOutputMode::Off));
		if(analogMode >= 0 && emu88Lib::isAnalogOutputModeValue(static_cast<uint32_t>(analogMode)))
			m_analogOutputMode = static_cast<emu88Lib::AnalogOutputMode>(analogMode);

		// The launcher has already checked the card.
		if(standaloneLaunch)
			m_pcmCard = loadPcmCard(*standaloneLaunch);
		auto params = createDeviceParams(m_deviceModel);
		m_device = std::make_unique<emu88Lib::HardwareDevice>(params, bootOptions(), m_pcmCard);
		// Before the engine sees the device, so the first host rate it negotiates already
		// includes the model's oversampling.
		m_device->setAnalogOutputMode(m_analogOutputMode);
		m_engine = std::make_unique<synthLib::Plugin>(m_device.get(), [](synthLib::Device*) { return nullptr; });
		// The boards are sound modules, not sequencers: the host clock the
		// engine would otherwise generate is just traffic on their MIDI in.
		m_engine->setMidiClockEnabled(false);
		m_engine->setLatencyBlocks(0);

		const auto configuredMode = m_config->getIntValue("resamplerMode",
			static_cast<int>(synthLib::Resampler::Mode::MameHq));
		const auto mode = configuredMode >= 0 && configuredMode < static_cast<int>(synthLib::Resampler::Mode::Count)
			? static_cast<synthLib::Resampler::Mode>(configuredMode)
			: synthLib::Resampler::Mode::MameHq;
		m_resamplerMode.store(static_cast<int>(mode), std::memory_order_relaxed);
		m_engine->setResamplerMode(mode);

		m_limiterEnabled.store(m_config->getBoolValue("outputLimiter", false));
		const auto gain = static_cast<float>(m_config->getDoubleValue(g_outputGainKey, kUnityOutputGain));
		m_outputGain.store(std::clamp(gain, kMinimumOutputGain, kMaximumOutputGain),
		                   std::memory_order_relaxed);

		// Start the bridge only on backends that provide virtual endpoints.
		const auto portMidiEnabled = jucePlayer::PortMidiBridge::virtualPortsSupported() &&
			m_config->getBoolValue("portMidiEnabled", true);
		m_portMidiEnabled.store(portMidiEnabled, std::memory_order_release);
		if(jucePlayer::PortMidiBridge::virtualPortsSupported())
		{
			m_portMidiBridge = std::make_unique<jucePlayer::PortMidiBridge>([this](synthLib::SMidiEvent _event)
			{
				const juce::ScopedLock lock(getCallbackLock());
				if(m_engine && _event.port < midiPortCount())
					m_engine->addMidiEvent(_event);
			}, m_config->getValue("virtualPortName", "88emu").toStdString(), 4);
			m_portMidiBridge->setEnabled(portMidiEnabled);
		}
	}

	Processor::~Processor()
	{
		// A take that was never saved is scratch data, so it goes with the app.
		stopRecording().deleteFile();
		m_portMidiBridge.reset();
	}

	std::unique_ptr<juce::PropertiesFile> Processor::createConfig(const std::string& _dataFolder)
	{
		juce::PropertiesFile::Options options;
		options.applicationName = "DSP56300Emulator_SC88Hardware";
		options.filenameSuffix = ".settings";
		options.folderName = "DSP56300Emulator_SC88Hardware";
		options.osxLibrarySubFolder = "Application Support/DSP56300Emulator_SC88Hardware";

		const auto configFolder = juce::File(_dataFolder).getChildFile("config");
		(void)configFolder.createDirectory();
		auto config = std::make_unique<juce::PropertiesFile>(
			configFolder.getChildFile("88emuPlayer.xml"), options);

		// Older builds let JUCE's standalone wrapper keep audio/MIDI and window
		// state in its platform-default file. Import those values once when the
		// consolidated Documents configuration does not already contain them.
		juce::PropertiesFile::Options legacyOptions;
		legacyOptions.applicationName = "88emuPlayer";
		legacyOptions.filenameSuffix = ".settings";
		legacyOptions.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
		legacyOptions.folderName = "~/.config";
#endif
		juce::PropertiesFile legacy(legacyOptions);
		bool imported = false;
		for(const auto* key : {"audioSetup", "shouldMuteInput", "windowX", "windowY", "lastStateFile"})
		{
			if(config->containsKey(key) || !legacy.containsKey(key))
				continue;
			config->setValue(key, legacy.getValue(key));
			imported = true;
		}
		if(imported)
			config->saveIfNeeded();

		return config;
	}

	bool Processor::isModelAvailable(const emu88Lib::DeviceModel _model)
	{
		return emu88Lib::isDeviceListed(_model) && emu88Lib::RomLoader::isDeviceAvailable(_model);
	}

	std::optional<emu88Lib::DeviceModel> Processor::firstAvailableModel()
	{
		for(const auto model : emu88Lib::g_deviceMenuOrder)
		{
			if(isModelAvailable(model))
				return model;
		}
		return {};
	}

	synthLib::DeviceCreateParams Processor::createDeviceParams(const emu88Lib::DeviceModel _model) const
	{
		synthLib::DeviceCreateParams params;
		params.homePath = m_dataFolder;
		params.customData = static_cast<uint32_t>(_model);
		params.romName = emu88Lib::getDeviceProfile(_model).displayName;

		return params;
	}

	emu88Lib::BootOptions Processor::bootOptions() const
	{
		emu88Lib::BootOptions boot;
		boot.factoryReset = m_config->getBoolValue(g_factoryResetOnLoadKey, boot.factoryReset);
		boot.fastBoot = m_config->getBoolValue(g_fastBootKey, boot.fastBoot);
		return boot;
	}

	void Processor::setBootOptions(const emu88Lib::BootOptions& _boot)
	{
		m_config->setValue(g_factoryResetOnLoadKey, _boot.factoryReset);
		m_config->setValue(g_fastBootKey, _boot.fastBoot);
		m_config->saveIfNeeded();
	}

	bool Processor::setDeviceModel(const emu88Lib::DeviceModel _model)
	{
		if(!emu88Lib::isDeviceModelValue(static_cast<uint32_t>(_model)))
			return false;

		if(_model == m_deviceModel && hasValidRom())
		{
			m_config->setValue(g_deviceModelKey, static_cast<int>(_model));
			m_config->saveIfNeeded();
			return true;
		}
		return replaceDevice(_model, true);
	}

	bool Processor::restartDevice()
	{
		return replaceDevice(m_deviceModel, false);
	}

	bool Processor::setPower(const bool enabled, const uint32_t heldButtons)
	{
		if(enabled == isPoweredOn()) return !enabled || hasValidRom();
		if(enabled) return replaceDevice(m_deviceModel, false, heldButtons);

		std::unique_ptr<emu88Lib::HardwareDevice> previousDevice;
		std::unique_ptr<synthLib::Plugin> previousEngine;
		{
			const juce::ScopedLock lock(getCallbackLock());
			m_midiPlayer.stop();
			std::vector<synthLib::SMidiEvent> discarded;
			m_midiPlayer.processBlock(discarded, 1, std::max(1.0, getSampleRate()), false);
			previousEngine = std::move(m_engine);
			previousDevice = std::move(m_device);
		}
		previousEngine.reset();
		previousDevice.reset();
		return true;
	}

	bool Processor::replaceDevice(const emu88Lib::DeviceModel _model, const bool _persistModel, const uint32_t heldButtons)
	{
		if(!emu88Lib::isDeviceModelValue(static_cast<uint32_t>(_model)))
			return false;

		// Switching or restarting the board is exactly when the user has just
		// dropped the missing dump into the ROM folder, so look again.
		(void)emu88Lib::RomLoader::rescan();

		auto boot = bootOptions();
		boot.initialPanelButtons = heldButtons;
		auto replacementDevice = std::make_unique<emu88Lib::HardwareDevice>(createDeviceParams(_model), boot, m_pcmCard);
		replacementDevice->setAnalogOutputMode(m_analogOutputMode);
		auto replacementEngine = std::make_unique<synthLib::Plugin>(
			replacementDevice.get(), [](synthLib::Device*) { return nullptr; });
		replacementEngine->setMidiClockEnabled(false);
		replacementEngine->setLatencyBlocks(0);
		if(getSampleRate() > 0.0)
			replacementEngine->setHostSamplerate(static_cast<float>(getSampleRate()), 0.0f);
		if(getBlockSize() > 0)
			replacementEngine->setBlockSize(static_cast<uint32_t>(getBlockSize()));
		replacementEngine->setResamplerMode(resamplerMode());

		m_midiPlayer.stop();
		suspendProcessing(true);
		std::unique_ptr<emu88Lib::HardwareDevice> previousDevice;
		std::unique_ptr<synthLib::Plugin> previousEngine;
		{
			const juce::ScopedLock lock(getCallbackLock());
			previousEngine = std::move(m_engine);
			previousDevice = std::move(m_device);
			m_device = std::move(replacementDevice);
			m_engine = std::move(replacementEngine);
			m_deviceModel = _model;
			m_midiPlayer.setPortCount(midiPortCount());
		}
		suspendProcessing(false);

		// The engine only borrows its device, so destroy it before the board it
		// references. Do this after resuming callbacks to keep teardown off the
		// real-time path.
		previousEngine.reset();
		previousDevice.reset();

		if(_persistModel)
		{
			m_config->setValue(g_deviceModelKey, static_cast<int>(_model));
			m_config->saveIfNeeded();
		}
		return hasValidRom();
	}

	void Processor::sendMidiEvents(const std::vector<synthLib::SMidiEvent>& _events)
	{
		const juce::ScopedLock lock(getCallbackLock());
		if(!m_engine || !m_device || !m_device->isValid())
			return;
		for(const auto& event : _events)
			m_engine->addMidiEvent(event);
	}

	uint8_t Processor::midiPortCount() const
	{
		// One logical port per part group: two on the SC-88/VL/Pro, four on the
		// SC-8850 (USB cables A-D), one on the SC-55mk2.
		return std::max<uint8_t>(1, emu88Lib::getDeviceProfile(m_deviceModel).groupCount);
	}

	void Processor::sendSystemExclusive(const std::initializer_list<uint8_t> _bytes)
	{
		const auto portCount = midiPortCount();
		std::vector<synthLib::SMidiEvent> events;
		events.reserve(portCount);
		for(uint8_t port = 0; port < portCount; ++port)
		{
			auto& event = events.emplace_back(synthLib::MidiEventSource::Host);
			event.sysex.assign(_bytes.begin(), _bytes.end());
			event.port = port;
		}
		sendMidiEvents(events);
	}

	void Processor::sendGmReset()
	{
		sendSystemExclusive({0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7});
	}

	void Processor::sendGsReset()
	{
		sendSystemExclusive({0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7});
	}

	void Processor::sendAllNotesOff()
	{
		const auto portCount = midiPortCount();
		std::vector<synthLib::SMidiEvent> events;
		events.reserve(static_cast<size_t>(portCount) * 16);
		for(uint8_t port = 0; port < portCount; ++port)
			for(uint8_t channel = 0; channel < 16; ++channel)
			{
				auto& event = events.emplace_back(synthLib::MidiEventSource::Host,
					static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | channel),
					synthLib::MC_ALLNOTESOFF, 0);
				event.port = port;
			}
		sendMidiEvents(events);
	}

	void Processor::prepareToPlay(const double _sampleRate, const int _maximumBlockSize)
	{
		m_outputLimiter.prepare(_sampleRate);
		for(auto& collector : m_liveMidi)
			collector.reset(_sampleRate);
		if(!m_engine)
			return;
		m_engine->setHostSamplerate(static_cast<float>(_sampleRate), 0.0f);
		m_engine->setBlockSize(static_cast<uint32_t>(std::max(1, _maximumBlockSize)));
		m_engine->setResamplerMode(resamplerMode());
	}

	void Processor::setOutputLimiterEnabled(bool enabled)
	{
		m_limiterEnabled.store(enabled);
		m_config->setValue("outputLimiter", enabled);
		m_config->saveIfNeeded();
	}

	void Processor::setOutputGain(const float _gain)
	{
		const auto gain = std::clamp(_gain, kMinimumOutputGain, kMaximumOutputGain);
		m_outputGain.store(gain, std::memory_order_relaxed);
		// The knob emits a change per pixel of a drag. PropertiesFile coalesces
		// these on its own save timer, so this must not saveIfNeeded() itself.
		m_config->setValue(g_outputGainKey, gain);
	}

	synthLib::Resampler::Mode Processor::resamplerMode() const
	{
		return static_cast<synthLib::Resampler::Mode>(m_resamplerMode.load(std::memory_order_relaxed));
	}

	void Processor::setResamplerMode(const synthLib::Resampler::Mode _mode)
	{
		if(_mode < synthLib::Resampler::Mode::Legacy || _mode >= synthLib::Resampler::Mode::Count)
			return;
		m_resamplerMode.store(static_cast<int>(_mode), std::memory_order_relaxed);
		if(m_engine)
			m_engine->setResamplerMode(_mode);
		m_config->setValue("resamplerMode", static_cast<int>(_mode));
		m_config->saveIfNeeded();
	}

	void Processor::setAnalogOutputMode(const emu88Lib::AnalogOutputMode _mode)
	{
		if(!emu88Lib::isAnalogOutputModeValue(static_cast<uint32_t>(_mode)))
			return;
		m_analogOutputMode = _mode;
		m_config->setValue("analogOutputMode", static_cast<int>(_mode));
		m_config->saveIfNeeded();

		// A model that oversamples changes the device rate. The board and the engine's
		// resampler switch together, between two audio callbacks.
		const juce::ScopedLock lock(getCallbackLock());
		if(!m_device || !m_engine)
			return;
		m_device->setAnalogOutputMode(_mode);
		if(getSampleRate() > 0.0)
			m_engine->setPreferredDeviceSamplerate(0.0f);
	}

	void Processor::setPortMidiEnabled(const bool _enabled)
	{
		if(!jucePlayer::PortMidiBridge::virtualPortsSupported())
			return;
		m_portMidiEnabled.store(_enabled, std::memory_order_release);
		if(m_portMidiBridge)
			m_portMidiBridge->setEnabled(_enabled);
		m_config->setValue("portMidiEnabled", _enabled);
		m_config->saveIfNeeded();
	}

	std::unique_ptr<juce::PropertiesFile> Processor::takeConfigOwnership()
	{
		return std::move(m_ownedConfig);
	}

	void Processor::useConfig(juce::PropertiesFile& _config)
	{
		m_ownedConfig.reset();
		m_config = &_config;
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layouts) const
	{
		return _layouts.getMainInputChannelSet().isDisabled() &&
		       _layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
	}

	void Processor::handleAsyncUpdate()
	{
		if(m_engine) m_engine->applyPendingDeviceSamplerate();
	}

	void Processor::addLiveMidi(const juce::MidiMessage& _message, const uint8_t _groups)
	{
		for(uint8_t group = 0; group < m_liveMidi.size(); ++group)
			if(_groups & (1u << group))
				m_liveMidi[group].addMessageToQueue(_message);
	}

	void Processor::addMidiBuffer(const juce::MidiBuffer& _midi, const uint8_t _port, const synthLib::MidiEventSource _source)
	{
		jucePlayer::forEachMidiEvent(_midi, _port, _source,
			[this](synthLib::SMidiEvent event) { m_engine->addMidiEvent(event); });
	}

	void Processor::takeLiveMidi(const int _samples, const bool _deliver)
	{
		if(_samples <= 0)
			return;
		// A device with fewer part groups drops the others, as it does for the virtual ports.
		const auto groups = _deliver ? midiPortCount() : uint8_t{0};
		for(uint8_t group = 0; group < m_liveMidi.size(); ++group)
		{
			m_liveMidiBlock.clear();
			m_liveMidi[group].removeNextBlockOfMessages(m_liveMidiBlock, _samples);
			if(group < groups)
				addMidiBuffer(m_liveMidiBlock, group, synthLib::MidiEventSource::Physical);
		}
		m_liveMidiBlock.clear();
	}

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		_buffer.clear();
		if(!m_engine || !m_device || !m_device->isValid())
		{
			_midi.clear();
			// Drop what arrived while off, rather than playing it all at the next power-on.
			takeLiveMidi(_buffer.getNumSamples(), false);
			std::vector<synthLib::SMidiEvent> discarded;
			m_midiPlayer.processBlock(discarded, static_cast<uint32_t>(_buffer.getNumSamples()), getSampleRate(), false);
			// Preserve the recording timeline while the board is off or unavailable.
			recordBlock(_buffer);
			return;
		}

		// The standalone detaches the player's merged input, so this is empty there; inputs arrive
		// through jucePlayer::MidiInputRouting, per part group.
		addMidiBuffer(_midi, 0, synthLib::MidiEventSource::Host);
		_midi.clear();
		takeLiveMidi(_buffer.getNumSamples(), true);

		std::vector<synthLib::SMidiEvent> playerEvents;
		m_midiPlayer.processBlock(playerEvents, static_cast<uint32_t>(_buffer.getNumSamples()),
		                          getSampleRate());
		for(auto& event : playerEvents)
			m_engine->addMidiEvent(event);

		// This standalone has no audio inputs.
		synthLib::TAudioInputs inputs{};
		synthLib::TAudioOutputs outputs{_buffer.getWritePointer(0), _buffer.getWritePointer(1)};
		m_engine->process(inputs, outputs, static_cast<size_t>(_buffer.getNumSamples()),
		                  120.0f, 0.0f, false, false);

		// The device can change its clock while playing. Switching the resampler over builds and
		// prewarms new filters, so Plugin only records the new rate here - apply it off this thread.
		if(m_engine->hasPendingDeviceSamplerate())
			triggerAsyncUpdate();
		_buffer.applyGain(outputGain());
		m_outputLimiter.process(_buffer.getWritePointer(0), _buffer.getWritePointer(1),
			static_cast<size_t>(_buffer.getNumSamples()), m_limiterEnabled.load());
		recordBlock(_buffer);
		// Hardware routing must not reverse the logical stereo recording.
		if(m_reverseOutputChannels.load())
			for(int i = 0; i < _buffer.getNumSamples(); ++i)
				std::swap(_buffer.getWritePointer(0)[i], _buffer.getWritePointer(1)[i]);

		std::vector<synthLib::SMidiEvent> midiOut;
		m_engine->getMidiOut(midiOut);
		for(const auto& event : midiOut)
		{
			if(m_portMidiBridge)
				m_portMidiBridge->enqueueOutput(event);

			juce::MidiMessage message;
			if(!event.sysex.empty() && event.sysex.size() >= 2)
				message = juce::MidiMessage::createSysExMessage(event.sysex.data() + 1,
					static_cast<int>(event.sysex.size() - 2));
			else
			{
				const uint8_t bytes[3]{event.a, event.b, event.c};
				const int length = synthLib::MidiBufferParser::lengthFromStatusByte(event.a);
				if(length <= 0 || length > 3)
					continue;
				message = juce::MidiMessage(bytes, length);
			}
			_midi.addEvent(message, static_cast<int>(std::min<uint32_t>(
				event.offset, static_cast<uint32_t>(std::max(0, _buffer.getNumSamples() - 1)))));
		}
	}

	bool Processor::startRecording()
	{
		if(m_recorder)
			return true;

		// The writer stamps the rate into the header once, so there has to be a
		// running audio device to take it from.
		const auto sampleRate = getSampleRate();
		if(sampleRate <= 0.0)
			return false;

		const auto name = "88emuPlayer " +
			juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".wav";
		auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile(name).getNonexistentSibling();

		auto recorder = std::make_unique<synthLib::AsyncWriter>(file.getFullPathName().toStdString(),
			static_cast<uint32_t>(sampleRate));

		// Sized here so the audio callback never grows the staging block itself.
		std::vector<uint32_t> block;
		block.reserve(static_cast<size_t>(std::max(1, getBlockSize())) * 2);

		const juce::ScopedLock lock(getCallbackLock());
		m_recordingFile = std::move(file);
		m_recordBlock = std::move(block);
		m_recordedFrames = 0;
		m_recorder = std::move(recorder);
		m_recording.store(true, std::memory_order_relaxed);
		return true;
	}

	juce::File Processor::stopRecording()
	{
		std::unique_ptr<synthLib::AsyncWriter> recorder;
		juce::File file;
		uint64_t frames = 0;

		{
			const juce::ScopedLock lock(getCallbackLock());
			recorder = std::move(m_recorder);
			file = m_recordingFile;
			frames = m_recordedFrames;
			m_recordingFile = juce::File();
			m_recordedFrames = 0;
			m_recording.store(false, std::memory_order_relaxed);
		}

		if(!recorder)
			return {};

		// Draining and joining the writer thread happens outside the lock: it can
		// take up to one of its poll intervals and must not stall the audio callback.
		recorder->setFinished();
		recorder.reset();

		if(!frames || !file.existsAsFile())
		{
			file.deleteFile();
			return {};
		}
		return file;
	}

	// Runs on the audio thread. synthLib::AsyncWriter owns the disk I/O thread,
	// which is what makes the length unbounded; staging a whole block first keeps
	// its mutex to one lock per callback instead of one per sample.
	void Processor::recordBlock(const juce::AudioBuffer<float>& _buffer)
	{
		if(!m_recorder)
			return;

		const auto frames = _buffer.getNumSamples();
		const auto* left = _buffer.getReadPointer(0);
		const auto* right = _buffer.getNumChannels() > 1 ? _buffer.getReadPointer(1) : left;

		// 24 bit two's complement, the word format the writer expects.
		constexpr auto fullScale = 8388607.0f;
		const auto to24 = [](const float _v)
		{
			return static_cast<uint32_t>(juce::roundToInt(std::clamp(_v, -1.0f, 1.0f) * fullScale)) & 0xffffff;
		};

		m_recordBlock.clear();
		for(int i = 0; i < frames; ++i)
		{
			m_recordBlock.push_back(to24(left[i]));
			m_recordBlock.push_back(to24(right[i]));
		}

		m_recorder->append([this](std::vector<uint32_t>& _out)
		{
			_out.insert(_out.end(), m_recordBlock.begin(), m_recordBlock.end());
		});
		m_recordedFrames += static_cast<uint64_t>(frames);
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new Editor(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new emu88Player::Processor();
}
