#include "PatchManager.h"

#include "VirusEditor.h"
#include "VirusController.h"
#include "VirusProcessor.h"

#include "jucePluginEditorLib/patchmanagerUiRml/patchmanagerUiRml.h"

#include "jucePluginLib/filetype.h"
#include "jucePluginLib/patchdb/datasource.h"

#include "virusLib/microcontroller.h"
#include "virusLib/device.h"
#include "virusLib/midiFileToRomData.h"

#include "synthLib/midiToSysex.h"

#include "juce_cryptography/hashing/juce_MD5.h"

#include "synthLib/sounddiverLibLoader.h"

namespace virus
{
	class Controller;
}

namespace genericVirusUI
{
	PatchManager::PatchManager(VirusEditor& _editor, Rml::Element* _root)
		: jucePluginEditorLib::patchManager::PatchManager(_editor, _root)
		, m_controller(_editor.getController())
		, m_processor(_editor.getProcessor())
		, m_onRomChanged(_editor.getProcessor().evRomChanged)
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "Virus Model");
		setTagTypeName(pluginLib::patchDB::TagType::CustomB, "Virus Features");

		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomB);

		startLoaderThread();

		// RAM banks are populated via MIDI when the controller receives them
		m_controller.onRomPatchReceived = [this](const virusLib::BankNumber _bank, const uint32_t _program)
		{
			if (_bank == virusLib::BankNumber::EditBuffer)
				return;

			const auto index = virusLib::toArrayIndex(_bank);

			// Only handle RAM banks (A and B = indices 0 and 1)
			if (index >= 2)
				return;

			const auto& banks = m_controller.getSinglePresets();

			if (index < banks.size() && _program == banks[index].size() - 1)
			{
				const auto ds = createDataSource(index);
				removeDataSource(ds);
				addDataSource(ds);
			}
		};

		// Add RAM banks that were already populated before the PatchManager was created
		{
			const auto& banks = m_controller.getSinglePresets();
			for (uint32_t i = 0; i < 2 && i < banks.size(); ++i)
			{
				if (!banks[i][0].data.empty())
					addDataSource(createDataSource(i));
			}
		}

		if (virusLib::isTIFamily(m_processor.getModel()))
		{
			addRomPatches();
		}
		else
		{
			// Initial ROM patches are added by the retained event, subsequent changes refresh
			m_onRomChanged = [this](const virusLib::ROMFile*)
			{
				addRomPatches();
			};
		}
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
		m_controller.onRomPatchReceived = {};
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, const uint32_t _bank, const uint32_t _program)
	{
		// RAM banks (0, 1) are loaded from live controller data (populated via MIDI)
		if (_bank < 2)
			return loadRamBankData(_results, _bank, _program);

		// ROM banks (2+) are loaded directly from ROM
		return loadRomBankData(_results, _bank - 2, _program);
	}

	bool PatchManager::loadRamBankData(pluginLib::patchDB::DataList& _results, const uint32_t _bank, const uint32_t _program)
	{
		const auto& singles = m_controller.getSinglePresets();

		if (_bank >= singles.size())
			return false;

		const auto& bank = singles[_bank];

		if (_program != pluginLib::patchDB::g_invalidProgram)
		{
			if (_program >= bank.size())
				return false;
			const auto& s = bank[_program];
			if (s.data.empty())
				return false;
			_results.push_back(s.data);
		}
		else
		{
			_results.reserve(bank.size());
			for (const auto& patch : bank)
			{
				if (!patch.data.empty())
					_results.push_back(patch.data);
			}
		}
		return !_results.empty();
	}

	bool PatchManager::loadRomBankData(pluginLib::patchDB::DataList& _results, const uint32_t _romBank, const uint32_t _program)
	{
		const auto* rom = m_processor.getSelectedRom();
		if (!rom)
			return false;

		const auto bankNumber = virusLib::fromArrayIndex(_romBank + 2);

		if (_program != pluginLib::patchDB::g_invalidProgram)
		{
			virusLib::ROMFile::TPreset preset;
			if (!rom->getSingle(static_cast<int>(_romBank), static_cast<int>(_program), preset))
				return false;
			if (virusLib::ROMFile::getSingleName(preset).size() != 10)
				return false;
			_results.push_back(virusLib::Microcontroller::createSingleDump(*rom, bankNumber, static_cast<uint8_t>(_program), preset));
		}
		else
		{
			_results.reserve(virusLib::ROMFile::getSinglesPerBank());
			for (uint8_t p = 0; p < virusLib::ROMFile::getSinglesPerBank(); ++p)
			{
				virusLib::ROMFile::TPreset preset;
				if (!rom->getSingle(static_cast<int>(_romBank), p, preset))
					continue;
				if (virusLib::ROMFile::getSingleName(preset).size() != 10)
					continue;
				_results.push_back(virusLib::Microcontroller::createSingleDump(*rom, bankNumber, p, preset));
			}
		}
		return !_results.empty();
	}

	std::shared_ptr<pluginLib::patchDB::Patch> PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
	{
		if (_sysex.size() < 267)
			return nullptr;

		const auto& c = static_cast<const virus::Controller&>(m_controller);

		pluginLib::MidiPacket::Data data;
		pluginLib::MidiPacket::AnyPartParamValues parameterValues;

		if (!c.parseSingle(data, parameterValues, _sysex))
			return nullptr;

		const auto idxVersion = c.getParameterIndexByName("Version");
		const auto idxCategory1 = c.getParameterIndexByName("Category1");
		const auto idxCategory2 = c.getParameterIndexByName("Category2");
		const auto idxUnison = c.getParameterIndexByName("Unison Mode");
//		const auto idxTranspose = c.getParameterIndexByName("Transpose");
		const auto idxArpMode = c.getParameterIndexByName("Arp Mode");
		const auto idxPhaserMix = c.getParameterIndexByName("Phaser Mix");
		const auto idxChorusMix = c.getParameterIndexByName("Chorus Mix");

		auto patch = std::make_shared<pluginLib::patchDB::Patch>();

		{
			const auto it = data.find(pluginLib::MidiDataType::Bank);
			if (it != data.end())
				patch->bank = it->second;
		}
		{
			const auto it = data.find(pluginLib::MidiDataType::Program);
			if (it != data.end())
				patch->program = it->second;
		}

		{
			constexpr auto frontOffset = 9;			// remove bank number, program number and other stuff that we don't need, first index is the patch version
			constexpr auto backOffset = 2;			// remove f7 and checksum
			const juce::MD5 md5(_sysex.data() + frontOffset, _sysex.size() - frontOffset - backOffset);

			static_assert(sizeof(juce::MD5) >= sizeof(pluginLib::patchDB::PatchHash));
			memcpy(patch->hash.data(), md5.getChecksumDataArray(), std::size(patch->hash));
		}

		patch->sysex = std::move(_sysex);

		patch->name = m_controller.getSinglePresetName(parameterValues);

		const auto version = virusLib::Microcontroller::getPresetVersion(*parameterValues[idxVersion]);
		const auto unison = *parameterValues[idxUnison];
//		const auto transpose = parameterValues.find(std::make_pair(pluginLib::MidiPacket::AnyPart, idxTranspose))->second;
		const auto arpMode = *parameterValues[idxArpMode];

		const auto category1 = *parameterValues[idxCategory1];
		const auto category2 = *parameterValues[idxCategory2];

		const auto* paramCategory1 = c.getParameter(idxCategory1, 0);
		const auto* paramCategory2 = c.getParameter(idxCategory2, 0);

		auto addCategory = [&patch, version](const uint8_t _value, const pluginLib::Parameter* _param)
		{
			if(!_value)
				return;
			const auto& values = _param->getDescription().valueList;
			if(_value >= values.texts.size())
				return;

			// Virus < TI had less categories
			if(version < virusLib::D && _value > 16)
				return;

			const auto t = _param->getDescription().valueList.valueToText(_value);
			patch->tags.add(pluginLib::patchDB::TagType::Category, t);
		};

		addCategory(category1, paramCategory1);
		addCategory(category2, paramCategory2);

		switch (version)
		{
		case virusLib::A:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "A");		break;
		case virusLib::B:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "B");		break;
		case virusLib::C:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "C");		break;
		case virusLib::D:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "TI");		break;
		case virusLib::D2:	patch->tags.add(pluginLib::patchDB::TagType::CustomA, "TI2");		break;
		}

		if(arpMode)
			patch->tags.add(pluginLib::patchDB::TagType::CustomB, "Arp");
		if(unison)
			patch->tags.add(pluginLib::patchDB::TagType::CustomB, "Unison");
		if(*parameterValues[idxPhaserMix] > 0)
			patch->tags.add(pluginLib::patchDB::TagType::CustomB, "Phaser");
		if(*parameterValues[idxChorusMix] > 0)
			patch->tags.add(pluginLib::patchDB::TagType::CustomB, "Chorus");
		return patch;
	}

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const
	{
		if (_patch->sysex.size() < 267)
			return _patch->sysex;

		if (_patch->sysex[6] != virusLib::SysexMessageType::DUMP_SINGLE)
			return _patch->sysex;

		auto result = _patch->sysex;

		// apply name
		if (!_patch->getName().empty())
		{
			for (size_t i=0; i<10; ++i)
				result[i + 249] = i >= _patch->getName().size() ? ' ' : _patch->getName()[i];
		}

		auto& bank = result[7];
		auto& program = result[8];

		// apply program
		if (_patch->program != pluginLib::patchDB::g_invalidProgram)
		{
			const auto bankOffset = _patch->program / 128;
			program = static_cast<uint8_t>(_patch->program - bankOffset * 128);
			bank += static_cast<uint8_t>(bankOffset);
		}

		// apply categories
		const uint32_t indicesCategory[] = {
			m_controller.getParameterIndexByName("Category1"),
			m_controller.getParameterIndexByName("Category2")
		};

		const pluginLib::Parameter* paramsCategory[] = {
			m_controller.getParameter(indicesCategory[0], 0),
			m_controller.getParameter(indicesCategory[1], 0)
		};

		uint8_t val0 = 0;
		uint8_t val1 = 0;

		const auto& tags = _patch->getTags(pluginLib::patchDB::TagType::Category);

		size_t i = 0;
		for (const auto& tag : tags.getAdded())
		{
			const auto categoryValue = paramsCategory[i]->getDescription().valueList.textToValue(tag);
			if(categoryValue != 0)
			{
				auto& v = i ? val1 : val0;
				v = static_cast<uint8_t>(categoryValue);
				++i;
				if (i == 2)
					break;
			}
		}

		result[249 + 11] = val0;
		result[249 + 12] = val1;

		result[265] = virusLib::Microcontroller::calcChecksum(result, 267 - 2);

		if (result.size() > 267)
			result[result.size() - 2] = virusLib::Microcontroller::calcChecksum(result, result.size() - 2);

		return result;
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename)
	{
		// Convert SysexBuffer to std::vector<uint8_t> for SounddiverLibLoader
		std::vector<uint8_t> dataVec(_data.begin(), _data.end());
		
		if (synthLib::SounddiverLibLoader::isValidData(dataVec))
		{
			synthLib::SounddiverLibLoader sd2s(dataVec);

			const auto& results = sd2s.getResults();

			if (!results.empty())
			{
				uint8_t prog = 0;

				for (const auto & res : results)
				{
					if (res.data.size() != 250)
						continue;

					// preset, pack into sysex

					synthLib::SysexBuffer& sysex = _results.emplace_back(
						synthLib::SysexBuffer{0xf0, 0x00, 0x20, 0x33, 0x01, virusLib::OMNI_DEVICE_ID, 0x10,
							static_cast<uint8_t>(prog >> 7), static_cast<uint8_t>(prog & 0x7f)}
					);

					sysex.insert(sysex.end(), res.data.begin(), res.data.begin() + 240);

					for (size_t i=0; i<10; ++i)
						sysex.push_back(i < res.name.size() ? res.name[i] : ' ');

					sysex.insert(sysex.end(), res.data.begin() + 240, res.data.end() - 4);

					sysex.push_back(virusLib::Microcontroller::calcChecksum(sysex));
					sysex.push_back(0xf7);

					++prog;
				}

				return true;
			}
		}

		{
			std::vector<synthLib::SMidiEvent> events;
			virusLib::Device::parseTIcontrolPreset(events, _data);

			for (const auto& e : events)
			{
				if (!e.sysex.empty())
					_results.push_back(e.sysex);
			}

			if (!_results.empty())
				return true;
		}

		if (virusLib::Device::parseVTIBackup(_results, _data))
			return true;

		bool res = virusLib::Device::parsePowercorePreset(_results, _data);
		res |= synthLib::MidiToSysex::extractSysexFromData(_results, _data);

		if(!res)
		{
			// Attempt to extract TDM plugin presets

			if (!virusLib::Device::parseTDMPreset(_results, _data, _filename))
				return false;
		}

		if(!_results.empty())
		{
			if(_data.size() > 500000)
			{
				virusLib::MidiFileToRomData romLoader;

				for (const auto& result : _results)
				{
					if(!romLoader.add(result))
						break;
				}
				if(romLoader.isComplete())
				{
					const auto& data = romLoader.getData();

					if(data.size() > 0x10000)
					{
						// presets are written to ROM address 0x50000, the second half of an OS update is therefore at 0x10000
						constexpr ptrdiff_t startAddr = 0x10000;
						ptrdiff_t addr = startAddr;
						uint32_t index = 0;

						while(addr + 0x100 <= static_cast<ptrdiff_t>(data.size()))
						{
							std::vector<uint8_t> chunk(data.begin() + addr, data.begin() + addr + 0x100);

							// validate
//							const auto idxH = chunk[2];
							const auto idxL = chunk[3];

							if(/*idxH != (index >> 7) || */idxL != (index & 0x7f))
								break;

							bool validName = true;
							for(size_t i=240; i<240+10; ++i)
							{
								if(chunk[i] < 32 || chunk[i] > 128)
								{
									validName = false;
									break;
								}
							}

							if(!validName)
								continue;

							addr += 0x100;
							++index;
						}

						if(index > 0)
						{
							_results.clear();

							for(uint32_t i=0; i<index; ++i)
							{
								// pack into sysex
								synthLib::SysexBuffer& sysex = _results.emplace_back(synthLib::SysexBuffer
									{0xf0, 0x00, 0x20, 0x33, 0x01, virusLib::OMNI_DEVICE_ID, 0x10, static_cast<uint8_t>(0x01 + (i >> 7)), static_cast<uint8_t>(i & 0x7f)}
								);
								sysex.insert(sysex.end(), data.begin() + i * 0x100 + startAddr, data.begin() + i * 0x100 + 0x100 + startAddr);
								sysex.push_back(virusLib::Microcontroller::calcChecksum(sysex));
								sysex.push_back(0xf7);
							}
						}
					}
				}
			}

		}

		return !_results.empty();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part, uint64_t)
	{
		_data = m_controller.createSingleDump(static_cast<uint8_t>(_part), toMidiByte(virusLib::BankNumber::A), 0);
		return !_data.empty();
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::equals(const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b) const
	{
		pluginLib::MidiPacket::Data dataA, dataB;
		pluginLib::MidiPacket::AnyPartParamValues parameterValuesA, parameterValuesB;

		if (!m_controller.parseSingle(dataA, parameterValuesA, _a->sysex) || !m_controller.parseSingle(dataB, parameterValuesB, _b->sysex))
			return false;

		if(parameterValuesA.size() != parameterValuesB.size())
			return false;

		for(uint32_t i=0; i<parameterValuesA.size(); ++i)
		{
			const auto& itA = parameterValuesA[i];
			const auto& itB = parameterValuesB[i];

			if(!itA)
			{
				if(itB)
					return false;
				continue;
			}

			if(!itB)
				return false;

			auto vA = *itA;
			auto vB = *itB;

			if(vA != vB)
			{
				// parameters might be out of range because some dumps have values that are out of range indeed, clamp to valid range and compare again
				const auto* param = m_controller.getParameter(i);
				if(!param)
					return false;

				if(param->getDescription().isNonPartSensitive())
					continue;

				vA = static_cast<uint8_t>(param->getDescription().range.clipValue(vA));
				vB = static_cast<uint8_t>(param->getDescription().range.clipValue(vB));

				if(vA != vB)
					return false;
			}
		}
		return true;
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _part)
	{
		return m_controller.activatePatch(applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware), _part);
	}

	void PatchManager::removeRomPatches()
	{
		for (const auto& ds : m_romDataSources)
			removeDataSource(ds);
		m_romDataSources.clear();
	}

	void PatchManager::addRomPatches()
	{
		removeRomPatches();

		const auto* rom = m_processor.getSelectedRom();
		if (!rom)
			return;

		const auto bankCount = rom->getNumSingleBanks();

		for (uint32_t b = 0; b < bankCount; ++b)
		{
			// Validate entire bank — stop at first invalid bank (matches Microcontroller behavior)
			bool valid = true;
			for (uint8_t p = 0; p < virusLib::ROMFile::getSinglesPerBank(); ++p)
			{
				virusLib::ROMFile::TPreset preset;
				if (!rom->getSingle(static_cast<int>(b), p, preset) || virusLib::ROMFile::getSingleName(preset).size() != 10)
				{
					valid = false;
					break;
				}
			}
			if (!valid)
				break;

			auto ds = createDataSource(b + 2);  // +2: controller index (0=RAM A, 1=RAM B, 2+=ROM)
			addDataSource(ds);
			m_romDataSources.push_back(std::move(ds));
		}
	}

	pluginLib::patchDB::DataSource PatchManager::createDataSource(const uint32_t _controllerBank) const
	{
		pluginLib::patchDB::DataSource ds;
		ds.type = pluginLib::patchDB::SourceType::Rom;
		ds.bank = _controllerBank;
		ds.name = m_controller.getBankName(_controllerBank);
		ds.midiBankNumber = _controllerBank;
		return ds;
	}

}
