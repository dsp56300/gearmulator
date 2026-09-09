#include "Emu88Processor.h"

#include "Emu88Editor.h"
#include "Emu88PortMidiBridge.h"

#include "88lib/romloader.h"

#include <cstdlib>

#include "baseLib/filesystem.h"
#include "baseLib/md5.h"

#include "synthLib/os.h"
#include "synthLib/romLoader.h"

namespace emu88Player
{
	namespace
	{
		constexpr auto g_deviceModelKey = "deviceModel";
		constexpr auto g_outputGainKey = "outputGain";

		// The same location every other TUS plugin uses, which is not the same
		// thing as JUCE's userDocumentsDirectory: baseLib follows the XDG base
		// directory spec on Linux, where the data folder is $XDG_DATA_HOME or
		// ~/.local/share rather than ~/Documents. It also honours the
		// TUS_DATA_FOLDER override that pluginLib::Tools::getPublicDataFolder
		// applies for environments where the documents folder is not usable.
		std::string documentsFolder(const char* _product, const char* _subFolder = nullptr)
		{
			const auto* overrideFolder = std::getenv("TUS_DATA_FOLDER");
			const auto root = overrideFolder && *overrideFolder
				? baseLib::filesystem::validatePath(overrideFolder)
				: baseLib::filesystem::getSpecialFolderPath(
					baseLib::filesystem::SpecialFolderType::UserDocuments);

			auto folder = baseLib::filesystem::validatePath(root + "The Usual Suspects/" + _product + '/');
			if(_subFolder)
				folder = baseLib::filesystem::validatePath(folder + _subFolder + '/');
			return folder;
		}

	}

	Processor::Processor()
		: juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
		  m_dataFolder(documentsFolder("88emuPlayer")),
		  m_romFolder(documentsFolder("88emuPlayer", "roms")),
		  m_ownedConfig(createConfig(m_dataFolder)),
		  m_config(m_ownedConfig.get())
	{
		(void)juce::File(m_romFolder).createDirectory();
		// These three are ours, so they are searched recursively - a user can sort
		// a ROM collection into subfolders. The module and working directories are
		// added by synthLib itself and stay flat on purpose: a double-clicked
		// application runs with "/" as its working directory.
		synthLib::RomLoader::addSearchPath(m_romFolder, true);
		// The player read the SC-88 plugin's ROM folder before it had one of its own. It stays
		// a fallback so an existing install keeps working, but it is never created and the
		// missing-ROM dialog points at m_romFolder above.
		synthLib::RomLoader::addSearchPath(documentsFolder("SC-88", "roms"), true);
		// Earlier builds asked JUCE for the documents folder directly, which on Linux is
		// ~/Documents rather than the XDG data folder every other TUS plugin uses. Keep reading
		// from there so an install that predates the fix still finds its ROMs.
		synthLib::RomLoader::addSearchPath(baseLib::filesystem::validatePath(
			juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
				.getChildFile("The Usual Suspects").getFullPathName().toStdString()), true);
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(true));
		synthLib::RomLoader::addSearchPath(synthLib::getModulePath(false));

		const auto configuredModel = m_config->getIntValue(g_deviceModelKey,
			static_cast<int>(emu88Lib::DeviceModel::Sc88Pro));
		if(configuredModel >= 0 && emu88Lib::isDeviceModelValue(static_cast<uint32_t>(configuredModel)))
			m_deviceModel = static_cast<emu88Lib::DeviceModel>(configuredModel);

		// A board is only selectable once every ROM the registry lists for it is
		// present. If the configured one is not, start on the first that is
		// rather than booting into a dead device - the configured choice is left
		// in the config so it comes back once its ROMs turn up.
		if(!isModelAvailable(m_deviceModel))
		{
			if(const auto fallback = firstAvailableModel())
				m_deviceModel = *fallback;
		}

		auto params = createDeviceParams(m_deviceModel);
		m_device = std::make_unique<emu88Lib::HardwareDevice>(params);
		m_engine = std::make_unique<synthLib::Plugin>(m_device.get(), [](synthLib::Device*) { return nullptr; });
		// The boards are sound modules, not sequencers: the host clock the
		// engine would otherwise generate is just traffic on their MIDI in.
		m_engine->setMidiClockEnabled(false);

		const auto configuredMode = m_config->getIntValue("resamplerMode",
			static_cast<int>(synthLib::Resampler::Mode::MameHq));
		const auto mode = configuredMode >= 0 && configuredMode < static_cast<int>(synthLib::Resampler::Mode::Count)
			? static_cast<synthLib::Resampler::Mode>(configuredMode)
			: synthLib::Resampler::Mode::MameHq;
		m_resamplerMode.store(static_cast<int>(mode), std::memory_order_relaxed);
		m_engine->setResamplerMode(mode);

		const auto gain = static_cast<float>(m_config->getDoubleValue(g_outputGainKey, kUnityOutputGain));
		m_outputGain.store(std::clamp(gain, kMinimumOutputGain, kMaximumOutputGain),
		                   std::memory_order_relaxed);

		// Without virtual-port support there is nothing for the bridge to open,
		// and starting it anyway leaves a thread polling two null ports at 1 kHz.
		const auto portMidiEnabled = PortMidiBridge::virtualPortsSupported() &&
			m_config->getBoolValue("portMidiEnabled", true);
		m_portMidiEnabled.store(portMidiEnabled, std::memory_order_release);
		if(PortMidiBridge::virtualPortsSupported())
		{
			m_portMidiBridge = std::make_unique<PortMidiBridge>([this](synthLib::SMidiEvent _event)
			{
				const juce::ScopedLock lock(getCallbackLock());
				if(m_engine)
					m_engine->addMidiEvent(_event);
			});
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
		return emu88Lib::RomLoader::isDeviceAvailable(_model);
	}

	std::optional<emu88Lib::DeviceModel> Processor::firstAvailableModel()
	{
		for(uint32_t value = 0; value < emu88Lib::deviceModelCount(); ++value)
		{
			const auto model = static_cast<emu88Lib::DeviceModel>(value);
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

	bool Processor::replaceDevice(const emu88Lib::DeviceModel _model, const bool _persistModel)
	{
		if(!emu88Lib::isDeviceModelValue(static_cast<uint32_t>(_model)))
			return false;

		// Switching or restarting the board is exactly when the user has just
		// dropped the missing dump into the ROM folder, so look again.
		(void)emu88Lib::RomLoader::rescan();

		auto replacementDevice = std::make_unique<emu88Lib::HardwareDevice>(createDeviceParams(_model));
		auto replacementEngine = std::make_unique<synthLib::Plugin>(
			replacementDevice.get(), [](synthLib::Device*) { return nullptr; });
		replacementEngine->setMidiClockEnabled(false);
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
		if(!m_engine)
			return;
		m_engine->setHostSamplerate(static_cast<float>(_sampleRate), 0.0f);
		m_engine->setBlockSize(static_cast<uint32_t>(std::max(1, _maximumBlockSize)));
		m_engine->setResamplerMode(resamplerMode());
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

	void Processor::setPortMidiEnabled(const bool _enabled)
	{
		if(!PortMidiBridge::virtualPortsSupported())
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

	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		_buffer.clear();
		if(!m_engine || !m_device || !m_device->isValid())
		{
			// Silence is still output; recording it keeps the WAV timeline honest.
			recordBlock(_buffer);
			return;
		}

		for(const auto metadata : _midi)
		{
			const auto message = metadata.getMessage();
			synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
			event.offset = static_cast<uint32_t>(std::max(0, metadata.samplePosition));
			if(message.isSysEx())
			{
				event.sysex.push_back(0xf0);
				const auto* data = message.getSysExData();
				for(int i = 0; i < message.getSysExDataSize(); ++i)
					event.sysex.push_back(data[i]);
				event.sysex.push_back(0xf7);
			}
			else
			{
				const auto* data = message.getRawData();
				const int size = message.getRawDataSize();
				if(size == 0 || size > 3)
					continue;
				event.a = data[0];
				event.b = size > 1 ? data[1] : 0;
				event.c = size > 2 ? data[2] : 0;
			}
			m_engine->addMidiEvent(event);
		}
		_midi.clear();

		std::vector<synthLib::SMidiEvent> playerEvents;
		m_midiPlayer.processBlock(playerEvents, static_cast<uint32_t>(_buffer.getNumSamples()),
		                          getSampleRate());
		for(auto& event : playerEvents)
			m_engine->addMidiEvent(event);

		// This standalone has no audio inputs. Value-initialise every pointer:
		// leaving std::array's pointer elements indeterminate is undefined
		// behaviour and made the first valid-ROM audio callback crash under LTO.
		synthLib::TAudioInputs inputs{};
		synthLib::TAudioOutputs outputs{_buffer.getWritePointer(0), _buffer.getWritePointer(1)};
		m_engine->process(inputs, outputs, static_cast<size_t>(_buffer.getNumSamples()),
		                  120.0f, 0.0f, false, false);
		_buffer.applyGain(outputGain());
		recordBlock(_buffer);

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
