#include "jePatchManager.h"

#include "jeEditor.h"

#include "jeController.h"
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

		auto rom = jeLib::RomLoader::findROM();

		if (rom.isValid())
		{
			rom.getPresets(m_presets);
		}

		auto addRomBank = [this](const uint32_t _bank, const char _name)
		{
			pluginLib::patchDB::DataSource ds;
			ds.type = pluginLib::patchDB::SourceType::Rom;
			ds.bank = _bank;
			ds.name = std::string("ROM ") + _name;
			addDataSource(ds);
		};

		addRomBank(0, 'A');
		addRomBank(1, 'b');
		addRomBank(2, 'P');
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
		if (_bank < 2)
		{
			constexpr auto presetsPerBank = 64;
			const auto first = _bank * presetsPerBank;
			const auto last = first + presetsPerBank;

			for (size_t i=first; i<last; ++i)
			{
				if (i >= m_presets.size())
					return !_results.empty();

				pluginLib::patchDB::Data presetData;
				for (auto& j : m_presets[i])
					presetData.insert(presetData.end(), j.begin(), j.end());
				_results.emplace_back(std::move(presetData));
			}
			return true;
		}
		return false;
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
				i += std::size(jeLib::g_sysexHeader) + 4; // skip header + address
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

		auto result = s;
		for (size_t i=1; i<dumps.size(); ++i)
			result.insert(result.end(), dumps[i].begin(), dumps[i].end());
		return result;
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		return m_controller.sendSingle(applyModifications(_patch, pluginLib::FileType::Empty, pluginLib::ExportType::EmuHardware), _part);
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		pluginLib::patchDB::DataList data;

		if (!jucePluginEditorLib::patchManager::PatchManager::parseFileData(data, _data))
			return false;

		// patches might be split into multiple sysex messages, try to merge them
		std::vector<std::map<uint32_t, std::vector<pluginLib::patchDB::Data>>> sameAddressPatches;

		for (auto& d : data)
		{
			const auto addr = jeLib::State::getAddress(d);

			const auto area = jeLib::State::getAddressArea(addr);

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

			const auto key = addr & mask;

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
