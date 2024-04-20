#include "PluginProcessor.h"
#include "PluginEditorState.h"
#include "ParameterNames.h"

#include "../virusLib/romloader.h"

#include "../synthLib/deviceException.h"
#include "../synthLib/binarystream.h"
#include "../synthLib/os.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties, const std::vector<virusLib::ROMFile>& _roms)
	: jucePluginEditorLib::Processor(_busesProperties, _configOptions, _properties)
	, m_roms(_roms)
{
	evRomChanged.retain(getSelectedRom());

	m_clockTempoParam = getController().getParameterIndexByName(Virus::g_paramClockTempo);

	const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
	Processor::setLatencyBlocks(latencyBlocks);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
	destroyEditorState();
}

//==============================================================================

void AudioPluginAudioProcessor::processBpm(const float _bpm)
{
	// clamp to virus range, 63-190
	const auto bpmValue = juce::jmin(127, juce::jmax(0, static_cast<int>(_bpm)-63));
	const auto clockParam = getController().getParameter(m_clockTempoParam, 0);

	if (clockParam == nullptr || static_cast<int>(clockParam->getValueObject().getValue()) == bpmValue)
		return;

	clockParam->getValueObject().setValue(bpmValue);
}

bool AudioPluginAudioProcessor::setSelectedRom(const uint32_t _index)
{
	if(_index >= m_roms.size())
		return false;
	if(_index == m_selectedRom)
		return true;
	m_selectedRom = _index;

	try
	{
		synthLib::Device* device = createDevice();
		getPlugin().setDevice(device);
		(void)m_device.release();
		m_device.reset(device);

		evRomChanged.retain(getSelectedRom());

		return true;
	}
	catch(const synthLib::DeviceException& e)
	{
		juce::NativeMessageBox::showMessageBox(juce::MessageBoxIconType::WarningIcon,
			"Device creation failed:",
			std::string("Failed to create device:\n\n") + 
			e.what() + "\n\n"
			"Will continue using old ROM");
		return false;
	}
}

synthLib::Device* AudioPluginAudioProcessor::createDevice()
{
	const auto* rom = getSelectedRom();
	return new virusLib::Device(rom ? *rom : virusLib::ROMFile::invalid(), getPreferredDeviceSamplerate(), getHostSamplerate(), true);
}

pluginLib::Controller* AudioPluginAudioProcessor::createController()
{
	// force creation of device as the controller decides how to initialize based on the used ROM
	getPlugin();

	return new Virus::Controller(*this);
}

void AudioPluginAudioProcessor::saveChunkData(synthLib::BinaryStream& s)
{
	auto* rom = getSelectedRom();
	if(rom)
	{
		synthLib::ChunkWriter cw(s, "ROM ", 2);
		const auto romName = synthLib::getFilenameWithoutPath(rom->getFilename());
		s.write<uint8_t>(static_cast<uint8_t>(rom->getModel()));
		s.write(romName);
	}
	Processor::saveChunkData(s);
}

void AudioPluginAudioProcessor::loadChunkData(synthLib::ChunkReader& _cr)
{
	_cr.add("ROM ", 2, [this](synthLib::BinaryStream& _binaryStream, unsigned _version)
	{
		auto model = virusLib::DeviceModel::ABC;

		if(_version > 1)
			model = static_cast<virusLib::DeviceModel>(_binaryStream.read<uint8_t>());

		const auto romName = _binaryStream.readString();

		const auto& roms = getRoms();
		for(uint32_t i=0; i<static_cast<uint32_t>(roms.size()); ++i)
		{
			const auto& rom = roms[i];
			if(rom.getModel() == model && synthLib::getFilenameWithoutPath(rom.getFilename()) == romName)
				setSelectedRom(i);
		}
	});

	Processor::loadChunkData(_cr);
}
