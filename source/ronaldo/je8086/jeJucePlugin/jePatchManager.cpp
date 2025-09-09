#include "jePatchManager.h"

#include "jeEditor.h"

#include "jeController.h"
#include "jeLib/state.h"

#include "juce_cryptography/juce_cryptography.h"

namespace jeJucePlugin
{
	PatchManager::PatchManager(Editor& _editor, Rml::Element* _rootElement)
	: jucePluginEditorLib::patchManager::PatchManager(_editor, _rootElement, DefaultGroupTypes)
	, m_editor(_editor)
	, m_processor(_editor.getProcessor())
	, m_controller(_editor.geJeController())
	{
		setTagTypeName(pluginLib::patchDB::TagType::CustomA, "Type");
		addGroupTreeItemForTag(pluginLib::patchDB::TagType::CustomA);

		PatchManager::startLoaderThread();
	}

	PatchManager::~PatchManager()
	{
		stopLoaderThread();
	}

	bool PatchManager::requestPatchForPart(pluginLib::patchDB::Data& _data, uint32_t _part, uint64_t _userData)
	{
		return false;
	}

	bool PatchManager::loadRomData(pluginLib::patchDB::DataList& _results, uint32_t _bank, uint32_t _program)
	{
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
		return {};
	}

	uint32_t PatchManager::getCurrentPart() const
	{
		return m_controller.getCurrentPart();
	}

	bool PatchManager::activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part)
	{
		return true;
	}

	bool PatchManager::parseFileData(pluginLib::patchDB::DataList& _results, const pluginLib::patchDB::Data& _data)
	{
		pluginLib::patchDB::DataList data;

		if (!jucePluginEditorLib::patchManager::PatchManager::parseFileData(data, _data))
			return false;

		// patches might be split into multiple sysex messages, try to merge them
		std::map<uint32_t, std::vector<pluginLib::patchDB::Data>> sameAddressPatches;

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

			mask = ~mask;

			const auto key = addr & mask;

			sameAddressPatches[key].emplace_back(std::move(d));
		}

		for (auto& [key, patches] : sameAddressPatches)
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

		return true;
	}
}
