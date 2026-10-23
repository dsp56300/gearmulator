#include "VirusProcessor.h"
#include "VirusController.h"
#include "PatchManager.h"

#include <algorithm>

#include "baseLib/binarystream.h"
#include "baseLib/filesystem.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/deviceException.h"
#include "synthLib/lv2PresetExport.h"

namespace virus
{
	VirusProcessor::VirusProcessor(const BusesProperties& _busesProperties, const juce::PropertiesFile::Options& _configOptions, const pluginLib::Processor::Properties& _properties, const virusLib::DeviceModel _defaultModel)
		: Processor(_busesProperties, _configOptions, _properties)
		, m_defaultModel(_defaultModel)
	{
	}

	VirusProcessor::~VirusProcessor()
	{
		// The editor and the patch manager are gone already, see jucePluginEditorLib::FinalProcessor. The controller
		// has to go here, before the ROMs it reads
		destroyController();
	}

	//==============================================================================

	void VirusProcessor::processBpm(const float _bpm)
	{
		// clamp to virus range, 63-190
		const auto bpmValue = juce::jmin(127, juce::jmax(0, static_cast<int>(_bpm)-63));

		// this is the audio thread, the controller sets the parameter on the message thread
		static_cast<Controller&>(getController()).setHostClockTempo(bpmValue);
	}

	void VirusProcessor::postConstruct(std::function<std::vector<virusLib::ROMFile>()> _findRoms)
	{
		m_findRoms = std::move(_findRoms);

		// "ROM " version 1 had no model, it was always A/B/C
		setFirmwareImages(findNewFirmwareImages(), {"ROM ", 2,
			[this](baseLib::BinaryStream& _s, const uint32_t _index)
			{
				const auto* rom = getRom(_index);
				_s.write<uint8_t>(static_cast<uint8_t>(rom->getModel()));
				_s.write(baseLib::filesystem::getFilenameWithoutPath(rom->getFilename()));
			},
			[this](baseLib::BinaryStream& _s, const uint32_t _version) -> std::optional<uint32_t>
			{
				const auto model = _version > 1 ? static_cast<virusLib::DeviceModel>(_s.read<uint8_t>()) : virusLib::DeviceModel::ABC;
				const auto romName = _s.readString();

				// of two ROMs with that name, the one found last, as before
				const std::lock_guard lock(m_romsMutex);
				std::optional<uint32_t> found;
				for(uint32_t i=0; i<static_cast<uint32_t>(m_roms.size()); ++i)
				{
					if(m_roms[i]->getModel() == model && baseLib::filesystem::getFilenameWithoutPath(m_roms[i]->getFilename()) == romName)
						found = i;
				}
				return found;
			}});

		// processBpm() uses the controller on the audio thread, it has to exist before audio runs
		getController();

		m_onFirmwareChanged.set(evFirmwareChanged, [this](uint32_t)
		{
			zynthianExportLv2Presets();
		});

		setPatchManager(new genericVirusUI::PatchManager(*this));
	}

	const virusLib::ROMFile* VirusProcessor::getRom(const uint32_t _index) const
	{
		const std::lock_guard lock(m_romsMutex);
		return _index < m_roms.size() ? m_roms[_index].get() : nullptr;
	}

	std::vector<std::string> VirusProcessor::findNewFirmwareImages()
	{
		std::vector<std::string> names;

		for (auto& rom : m_findRoms())
		{
			// the same file as the same model, a TI firmware is listed once per model
			const auto listed = std::any_of(m_roms.begin(), m_roms.end(), [&rom](const auto& _listed)
			{
				return _listed->getModel() == rom.getModel() && _listed->getFilename() == rom.getFilename();
			});

			if (listed)
				continue;

			names.push_back(juce::File(rom.getFilename()).getFileNameWithoutExtension().toStdString() + " (" + rom.getModelName() + ')');

			const std::lock_guard lock(m_romsMutex);
			m_roms.push_back(std::make_unique<const virusLib::ROMFile>(std::move(rom)));
		}
		return names;
	}

	synthLib::Device* VirusProcessor::newDevice(const synthLib::DeviceCreateParams& _params)
	{
		return new virusLib::Device(_params, true);
	}

	void VirusProcessor::getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const
	{
		pluginLib::Processor::getRemoteDeviceParams(_params);

		const auto* rom = getSelectedRom();

		if(!rom || !rom->isValid())
		{
			if(isTIFamily(m_defaultModel))
				throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus TI firmware (.bin) is required, but was not found.");
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "A Virus A/B/C operating system (.bin or .mid) is required, but was not found.");
		}

		_params.romName = rom->getFilename();
		_params.romData = rom->getRomFileData();
		_params.customData = static_cast<uint32_t>(rom->getModel());
	}

	pluginLib::Controller* VirusProcessor::createController()
	{
		// force creation of device as the controller decides how to initialize based on the used ROM
		getPlugin();

		return new virus::Controller(*this, m_defaultModel);
	}

	void VirusProcessor::zynthianExportLv2Presets() const
	{
		const auto* rom = getSelectedRom();
		if(!rom)
			return;

#ifdef ZYNTHIAN
		constexpr const char* const rootPath = "/zynthian/zynthian-my-data/presets/lv2";
		exportLv2Presets(*rom, rootPath);
#elif defined(_WIN32) && defined(_DEBUG)
//		const auto rootPath = synthLib::getModulePath();
//		exportLv2Presets(*rom, rootPath);
#endif
	}

	void VirusProcessor::exportLv2Presets(const virusLib::ROMFile& _rom, const std::string& _rootPath) const
	{
		const auto bankCount = virusLib::ROMFile::getRomBankCount(_rom.getModel());

		for(uint32_t b=0; b<bankCount; ++b)
		{
			synthLib::Lv2PresetExport::Bank bank;

			switch (_rom.getModel())
			{
			case virusLib::DeviceModel::A:
			case virusLib::DeviceModel::B:
			case virusLib::DeviceModel::C:				bank.name = std::string("Rom "     ) + static_cast<char>('C' + b);	break;
			case virusLib::DeviceModel::Snow:			bank.name = std::string("Snow Rom ") + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::TI:				bank.name = std::string("TI Rom "  ) + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::TI2:			bank.name = std::string("TI2 Rom " ) + static_cast<char>('A' + b);	break;
			case virusLib::DeviceModel::Invalid:		bank.name = std::string("Unknown " ) + static_cast<char>('A' + b);	assert(false); break;
			}

			const auto path = baseLib::filesystem::validatePath(_rootPath) + getProperties().name + '_' + synthLib::Lv2PresetExport::getBankFilename(bank.name) + ".lv2/";

			if(synthLib::Lv2PresetExport::manifestFileExists(path))
				continue;

			for(uint8_t p=0; p<_rom.getPresetsPerBank(); ++p)
			{
				virusLib::ROMFile::TPreset preset;

				if(!_rom.getSingle(static_cast<uint8_t>(b), p, preset))
					continue;

				synthLib::Lv2PresetExport::Preset lv2Preset;

				lv2Preset.data = {0xf0, 0x00, 0x20, 0x33, 01, virusLib::OMNI_DEVICE_ID, virusLib::DUMP_SINGLE, virusLib::toMidiByte(virusLib::BankNumber::EditBuffer), virusLib::SINGLE};
				lv2Preset.data.insert(lv2Preset.data.end(), preset.begin(), preset.begin() + virusLib::ROMFile::getSinglePresetSize(_rom.getModel()));
				lv2Preset.data.push_back(virusLib::Microcontroller::calcChecksum(lv2Preset.data));
				lv2Preset.data.push_back(0xf7);

				lv2Preset.name = virusLib::ROMFile::getSingleName(preset);

				bank.presets.push_back(lv2Preset);
			}

			if(bank.presets.empty())
				continue;

			synthLib::Lv2PresetExport::exportPresets(path, getProperties().lv2Uri, bank);
		}
	}
}
