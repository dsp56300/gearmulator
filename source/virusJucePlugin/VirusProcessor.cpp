#include "VirusProcessor.h"
#include "VirusEditorState.h"
#include "ParameterNames.h"

#include "virusLib/romloader.h"

#include "baseLib/binarystream.h"

#include "synthLib/deviceException.h"
#include "synthLib/os.h"

namespace virus
{
	VirusProcessor::VirusProcessor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties, const std::vector<virusLib::ROMFile>& _roms, const virusLib::DeviceModel _defaultModel)
		: Processor(_busesProperties, _configOptions, _properties)
		, m_roms(_roms)
		, m_defaultModel(_defaultModel)
	{
	}

	VirusProcessor::~VirusProcessor()
	{
		destroyController();
		destroyEditorState();
	}

	//==============================================================================

	void VirusProcessor::processBpm(const float _bpm)
	{
		// clamp to virus range, 63-190
		const auto bpmValue = juce::jmin(127, juce::jmax(0, static_cast<int>(_bpm)-63));
		const auto clockParam = getController().getParameter(m_clockTempoParam, 0);

		if (clockParam == nullptr || clockParam->getUnnormalizedValue() == bpmValue)
			return;

		clockParam->setUnnormalizedValue(bpmValue, pluginLib::Parameter::Origin::HostAutomation);
	}

	bool VirusProcessor::setSelectedRom(const uint32_t _index)
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

	void VirusProcessor::postConstruct()
	{
		evRomChanged.retain(getSelectedRom());

		m_clockTempoParam = getController().getParameterIndexByName(virus::g_paramClockTempo);

		const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
		Processor::setLatencyBlocks(latencyBlocks);
	}

	synthLib::Device* VirusProcessor::createDevice()
	{
		const auto* rom = getSelectedRom();

		if(!rom || !rom->isValid())
		{
			if(isTIFamily(m_defaultModel))
				throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus TI firmware (.bin) is required, but was not found.");
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus A/B/C operating system (.bin or .mid) is required, but was not found.");
		}
		return new virusLib::Device(*rom, getPreferredDeviceSamplerate(), getHostSamplerate(), true);
	}

	pluginLib::Controller* VirusProcessor::createController()
	{
		// force creation of device as the controller decides how to initialize based on the used ROM
		getPlugin();

		return new virus::Controller(*this, m_defaultModel);
	}

	void VirusProcessor::saveChunkData(baseLib::BinaryStream& s)
	{
		auto* rom = getSelectedRom();
		if(rom)
		{
			baseLib::ChunkWriter cw(s, "ROM ", 2);
			const auto romName = synthLib::getFilenameWithoutPath(rom->getFilename());
			s.write<uint8_t>(static_cast<uint8_t>(rom->getModel()));
			s.write(romName);
		}
		Processor::saveChunkData(s);
	}

	void VirusProcessor::loadChunkData(baseLib::ChunkReader& _cr)
	{
		_cr.add("ROM ", 2, [this](baseLib::BinaryStream& _binaryStream, unsigned _version)
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
}