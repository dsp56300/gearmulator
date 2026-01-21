#include "jePatchManager.h"

#include "jeEditor.h"

#include "jeController.h"
#include "jePluginProcessor.h"
#include "jeLib/romloader.h"
#include "jeLib/state.h"
#include "jucePluginLib/filetype.h"

#include "juce_cryptography/juce_cryptography.h"

#include "synthLib/midiToSysex.h"

namespace jeJucePlugin
{
	PatchManager::PatchManager(Editor& _editor, Rml::Element* _rootElement)
	: jucePluginEditorLib::patchManager::PatchManager(_editor, _rootElement, DefaultGroupTypes)
	, m_editor(_editor)
	, m_processor(_editor.getProcessor())
	, m_controller(_editor.getJeController())
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "Type");
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);

		PatchManager::startLoaderThread();

		const auto& roms = _editor.getJeProcessor().getRoms();

		// add each device type only once
		std::set<jeLib::DeviceType> knownDeviceTypes;

		uint32_t bankIndex = 0;

		for (const auto& rom : roms)
		{
			if (!rom.isValid())
				continue;

			if (!knownDeviceTypes.insert(rom.getDeviceType()).second)
				continue;

			std::vector<std::vector<jeLib::Rom::Preset>> presets;
			rom.getPresets(presets);

			if (presets.empty())
				continue;

			for (auto p : presets)
				m_presetsPerBank.emplace_back(std::move(p));

			auto addRomBank = [this, &bankIndex](const std::string& _name)
			{
				pluginLib::patchDB::DataSource ds;
				ds.type = pluginLib::patchDB::SourceType::Rom;
				ds.bank = bankIndex++;
				ds.name = _name;
				addDataSource(ds);
			};

			if (presets.size() == 3)
			{
				// keyboard: two banks of patches, one bank of performances
				addRomBank("Keyboard - Patch P:A11 - P:A88");
				addRomBank("Keyboard - Patch P:B11 - P:B88");
				addRomBank("Keyboard - Performance P:11 - P:88");
			}
			else
			{
				addRomBank("Rack - Patch P1:A11 - P1:A88");
				addRomBank("Rack - Patch P1:B11 - P1:B88");
				addRomBank("Rack - Patch P2:A11 - P2:A88");
				addRomBank("Rack - Patch P2:B11 - P2:B88");
				addRomBank("Rack - Patch P3:A11 - P3:A88");
				addRomBank("Rack - Patch P3:B11 - P3:B88");
				addRomBank("Rack - Patch P4:A11 - P4:A88");
				addRomBank("Rack - Patch P4:B11 - P4:B88");
				addRomBank("Rack - Performance P1:11 - P1:88");
				addRomBank("Rack - Performance P2:11 - P2:88");
				addRomBank("Rack - Performance P3:11 - P3:88");
				addRomBank("Rack - Performance P4:11 - P4:88");
			}
		}
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, const uint32_t _part, const uint64_t _userData)
	{
		return m_controller.requestPatchForPart(_data, _part, _userData);
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, const uint32_t _bank, uint32_t _program)
	{
		if (_bank >= m_presetsPerBank.size())
			return false;

		const auto& presets = m_presetsPerBank[_bank];

		if (presets.empty())
			return false;

		for (auto& p : presets)
		{
			auto preset = p.front();

			for (size_t i=1; i<p.size(); ++i)
			{
				preset.insert(preset.end(), p[i].begin(), p[i].end());
			}
			_results.emplace_back(std::move(preset));
		}

		return true;
	}

	pluginLib::patchDB::PatchPtr PatchManager::initializePatch(pluginLib::patchDB::Data&& _sysex, const std::string& _defaultPatchName)
	{
		const auto addr = jeLib::State::getAddress(_sysex);
		const auto area = jeLib::State::getAddressArea(addr);

		if (area != jeLib::AddressArea::UserPatch && area != jeLib::AddressArea::UserPerformance)
			return {};

		const auto bank = jeLib::State::getBankNumber(addr);
		const auto prog = jeLib::State::getProgramNumber(addr);

		const auto name = jeLib::State::getName(_sysex);

		if (!name)
			return {};

		// why this is so complicated: Multiple sysex messages are combined in _sysex, but we only want to hash the actual patch data
		std::vector<uint8_t> temp;
		temp.reserve(_sysex.size());

		for (size_t i=0; i<_sysex.size();)
		{
			if (_sysex[i] == 0xf0)
			{
				i += std::size(jeLib::g_sysexHeader) + 1 + 4; // skip header + command + address
				continue;
			}

			if (i < _sysex.size() - 1 && _sysex[i + 1] == 0xf7)
			{
				i += 2; // skip checksum + 0xf7
				continue;
			}

			temp.push_back(_sysex[i]);
			++i;
		}

		auto p = std::make_shared<pluginLib::patchDB::Patch>();
		p->name = *name;
		p->sysex = std::move(_sysex);
		p->program = prog;
		p->bank = bank;

		if (area == jeLib::AddressArea::UserPatch)
			p->tags.add(pluginLib::patchDB::TagType::CustomA, "Patch");
		else
			p->tags.add(pluginLib::patchDB::TagType::CustomA, "Performance");

		memcpy(p->hash.data(), juce::MD5(temp.data(), static_cast<int>(temp.size())).getChecksumDataArray(), sizeof(p->hash));

		return p;
	}

	pluginLib::patchDB::Data PatchManager::applyModifications(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const
	{
		std::vector<pluginLib::SysEx> dumps;
		synthLib::MidiToSysex::splitMultipleSysex(dumps, _patch->sysex);

		if (dumps.empty())
			return _patch->sysex;

		// apply mods to the first dump only, the rest should be identical
		auto& s = dumps.front();

		const auto addr = jeLib::State::getAddress(s);
		const auto area = jeLib::State::getAddressArea(addr);

		if (area != jeLib::AddressArea::UserPatch && area != jeLib::AddressArea::UserPerformance)
			return _patch->sysex;

		auto bank = _patch->program / 64;
		const auto program = _patch->program - bank * 64;
		bank += _patch->bank;

		jeLib::State::setBankNumber(s, static_cast<uint8_t>(bank));
		jeLib::State::setProgramNumber(s, static_cast<uint8_t>(program));
		jeLib::State::setName(s, _patch->getName());

		// recalculate checksum
		jeLib::State::updateChecksum(s);

		auto isPerformance = area == jeLib::AddressArea::UserPerformance;

		auto result = s;
		for (size_t i=1; i<dumps.size(); ++i)
		{
			// performance number needs to be encoded into all subsequent dumps as well
			if (isPerformance)
			{
				const auto performanceAddress = jeLib::State::getAddress(s) & ~static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockMask);

				auto a = jeLib::State::getAddress(dumps[i]);
				a &= static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockMask);
				a |= performanceAddress;
				jeLib::State::setAddress(dumps[i], a);

				jeLib::State::setProgramNumber(dumps[i], static_cast<uint8_t>(program));
				jeLib::State::updateChecksum(dumps[i]);
			}

			result.insert(result.end(), dumps[i].begin(), dumps[i].end());
		}
		return result;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		if (!m_controller.sendSingle(applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware), _part))
			return false;

		const auto area = jeLib::State::getAddressArea(_patch->sysex);

		if (area == jeLib::AddressArea::UserPerformance)
		{
			// performance applies to both parts, update selection for both of them
			for (uint8_t part = 0; part < m_controller.getPartCount(); ++part)
			{
				if (_part == part)
					continue;
				setSelectedPatch(part, _patch);
			}
		}

		return true;
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data, const std::string& _filename)
	{
		pluginLib::patchDB::DataList data;

		if (!jucePluginEditorLib::patchManager::PatchManager::parseFileData(data, _data, _filename))
		{
			// pfm/pat file?
			if (_data.size() > 7 + 12)
			{
				const auto isRack = _data[5] == '8';
				const auto isKeyboard = _data[5] == '0';

				if (!isRack && !isKeyboard)
					return false;

				auto createSysexFromRawData = [](uint32_t _addr, const synthLib::SysexBuffer::const_iterator& _begin, const synthLib::SysexBuffer::const_iterator& _end)
				{
					jeLib::State::Dump dump;
					dump.reserve(512);

					for (auto h : jeLib::g_sysexHeader)
						dump.push_back(static_cast<uint8_t>(h));

					dump.push_back(static_cast<uint8_t>(jeLib::SysexByte::CommandIdDataSet1));

					dump.push_back(0x00); // placeholder for address
					dump.push_back(0x00); // placeholder for address
					dump.push_back(0x00); // placeholder for address
					dump.push_back(0x00); // placeholder for address

					dump.insert(dump.end(), _begin, _end);

					for (auto h : jeLib::g_sysexFooter)
						dump.push_back(static_cast<uint8_t>(h));

					jeLib::State::setAddress(dump, _addr);
					jeLib::State::updateChecksum(dump);

					return dump;
				};

				constexpr auto programNoDiff = static_cast<uint32_t>(jeLib::UserPatchArea::UserPatch002) - static_cast<uint32_t>(jeLib::UserPatchArea::UserPatch001);
				constexpr auto performanceNoDiff = static_cast<uint32_t>(jeLib::UserPerformanceArea::UserPerformance02) - static_cast<uint32_t>(jeLib::UserPerformanceArea::UserPerformance01);

				constexpr size_t start = 0x80;

				uint32_t program = 0;

				if (std::string(reinterpret_cast<const char*>(&_data[7]), 12) == " USER PATCH ")
				{
					// this is a list of patches without sysex headers/footers, we generate sysex patches here

					const auto size = static_cast<ptrdiff_t>(isKeyboard ? jeLib::Patch::DataLengthKeyboard : jeLib::Patch::DataLengthRack);

					auto i = start;

					while (i + size <= _data.size())
					{
						// we do not expect more than 128 patches in a pfm/pat file but just to be on the safe
						// side we limit it here because the address area can only express A11 to B88
						program &= 0x7f;

						auto addr = static_cast<uint32_t>(jeLib::AddressArea::UserPatch);

						if (program < 64)
							addr += static_cast<uint32_t>(jeLib::UserPatchArea::UserPatch001) + programNoDiff * program;
						else
							addr += static_cast<uint32_t>(jeLib::UserPatchArea::UserPatch065) + programNoDiff * (program - 64);

						const auto is = static_cast<std::ptrdiff_t>(i);
						jeLib::State::Dump dump = createSysexFromRawData(addr, _data.begin() + is, _data.begin() + is + size);

						_results.emplace_back(std::move(dump));

						i += size;
						++program;
					}

					if (program > 0)
						return true;
				}
				else if (std::string(reinterpret_cast<const char*>(&_data[7]), 17) == " USER PERFORMANCE")
				{
					if (!isKeyboard)
						return false;	// no file for testing available so skip for now

					size_t i = start;

					constexpr auto sizePerformanceCommon = static_cast<ptrdiff_t>(jeLib::PerformanceCommon::DataLengthKeyboard);
					constexpr auto sizePart = static_cast<ptrdiff_t>(jeLib::Part::DataLengthKeyboard);
					constexpr auto sizePatch = static_cast<ptrdiff_t>(jeLib::Patch::DataLengthKeyboard);

					constexpr ptrdiff_t size =
						sizePerformanceCommon +
						sizePart +  // part upper
						sizePart +  // part lower
						sizePatch + // patch upper
						sizePatch;  // patch lower

					while (i + size <= _data.size())
					{
						program &= 0x3f;	// valid performance numbers are 0-63

						uint32_t addr = static_cast<uint32_t>(jeLib::AddressArea::UserPerformance) +
							static_cast<uint32_t>(jeLib::UserPerformanceArea::UserPerformance01) +
							performanceNoDiff * program;

						auto patch = createSysexFromRawData(addr, _data.begin() + static_cast<std::ptrdiff_t>(i), _data.begin() + static_cast<std::ptrdiff_t>(i + sizePerformanceCommon));
						i += sizePerformanceCommon;
						auto partUpper = createSysexFromRawData(addr | static_cast<uint32_t>(jeLib::PerformanceData::PartUpper), _data.begin() + static_cast<std::ptrdiff_t>(i), _data.begin() + static_cast<std::ptrdiff_t>(i + sizePart));
						i += sizePart;
						auto partLower = createSysexFromRawData(addr | static_cast<uint32_t>(jeLib::PerformanceData::PartLower), _data.begin() + static_cast<std::ptrdiff_t>(i), _data.begin() + static_cast<std::ptrdiff_t>(i + sizePart));
						i += sizePart;
						auto patchUpper = createSysexFromRawData(addr | static_cast<uint32_t>(jeLib::PerformanceData::PatchUpper), _data.begin() + static_cast<std::ptrdiff_t>(i), _data.begin() + static_cast<std::ptrdiff_t>(i + sizePatch));
						i += sizePatch;
						auto patchLower = createSysexFromRawData(addr | static_cast<uint32_t>(jeLib::PerformanceData::PatchLower), _data.begin() + static_cast<std::ptrdiff_t>(i), _data.begin() + static_cast<std::ptrdiff_t>(i + sizePatch));
						i += sizePatch;

						patch.insert(patch.end(), partUpper.begin(), partUpper.end());
						patch.insert(patch.end(), partLower.begin(), partLower.end());
						patch.insert(patch.end(), patchUpper.begin(), patchUpper.end());
						patch.insert(patch.end(), patchLower.begin(), patchLower.end());

						_results.push_back(std::move(patch));

						++program;
					}

					if (program > 0)
						return true;
				}
			}
			return false;
		}

		// patches might be split into multiple sysex messages, try to merge them
		std::vector<std::map<uint32_t, std::vector<pluginLib::patchDB::Data>>> sameAddressPatches;

		bool foundTempPerformanceCommon = false;

		for (auto& d : data)
		{
			auto addr = jeLib::State::getAddress(d);
			auto area = jeLib::State::getAddressArea(addr);

			// surprisingly there are indeeed files around that contain edit buffer dumps.
			// The problem is that we do not know if PatchUpper or PatchLower is part of a performance or individual.
			// We treat all of them individual if there is no PerformanceCommon dump
			if (area == jeLib::AddressArea::PerformanceTemp)
			{
				const auto type = static_cast<jeLib::PerformanceData>(addr & static_cast<uint32_t>(jeLib::PerformanceData::BlockMask));

				if (type == jeLib::PerformanceData::PerformanceCommon)
					foundTempPerformanceCommon = true;

				// if there is any performance common dump in there, treat all subsequent dumps as user performance dumps
				if (foundTempPerformanceCommon)
				{
					addr &= ~static_cast<uint32_t>(jeLib::AddressArea::PerformanceTemp);
					addr |= static_cast<uint32_t>(jeLib::AddressArea::UserPerformance);

					area = jeLib::State::getAddressArea(addr);
					jeLib::State::setAddress(d, addr);
				}
				else if (type == jeLib::PerformanceData::PatchUpper || type == jeLib::PerformanceData::PatchLower)
				{
					// otherwise, treat them as user patches at bank 0 program 0
					addr = static_cast<uint32_t>(jeLib::AddressArea::UserPatch);
					jeLib::State::setAddress(d, addr);
					area = jeLib::State::getAddressArea(addr);
				}
			}

			// group sysex messages that belong to the same patch

			if (area != jeLib::AddressArea::UserPatch && area != jeLib::AddressArea::UserPerformance)
			{
				_results.emplace_back(std::move(d));
				continue;
			}

			uint32_t mask;

			if (area == jeLib::AddressArea::UserPerformance)
				mask = static_cast<uint32_t>(jeLib::UserPerformanceArea::BlockMask);
			else
				mask = static_cast<uint32_t>(jeLib::UserPatchArea::BlockMask);

			if ((addr & mask) == 0)
				sameAddressPatches.emplace_back(); // new patch/performance, create new map

			mask = ~mask;

			// ignore the key. We messed up patch saving in a previous version that causes some
			// dumps to point to performance 0 all the time.
			constexpr uint32_t key = 0;//addr & mask;

			// this can only happen if the dumps are corrupted
			assert(!sameAddressPatches.empty());
			if (sameAddressPatches.empty())
				continue;

			sameAddressPatches.back()[key].emplace_back(std::move(d));
		}

		for (auto& p : sameAddressPatches)
		{
			for (auto& [key, patches] : p)
			{
				if (patches.size() == 1)
				{
					_results.emplace_back(std::move(patches.front()));
					continue;
				}

				auto& e = _results.emplace_back();

				for (auto& p : patches)
					e.insert(e.end(), p.begin(), p.end());
			}
		}

		return true;
	}
}
