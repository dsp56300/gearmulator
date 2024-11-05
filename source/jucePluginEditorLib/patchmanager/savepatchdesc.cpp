#include "savepatchdesc.h"

#include "patchmanager.h"

#include "synthLib/sysexToMidi.h"

namespace jucePluginEditorLib::patchManager
{
	SavePatchDesc::SavePatchDesc(PatchManager& _pm, const int _part, std::string _name)
		: m_patchManager(_pm)
		, m_part(_part)
		, m_name(std::move(_name))
	{
	}

	SavePatchDesc::SavePatchDesc(PatchManager& _pm, std::map<uint32_t, pluginLib::patchDB::PatchPtr>&& _patches, std::string _name)
		: m_patchManager(_pm)
        , m_part(InvalidPart)
        , m_patches(std::move(_patches))
        , m_name(std::move(_name))
	{
	}

	std::map<uint32_t, pluginLib::patchDB::PatchPtr>& SavePatchDesc::getPatches() const
	{
		if(m_patches.empty() && isPartValid())
		{
			auto patch = m_patchManager.requestPatchForPart(m_part);
			if(!patch)
				return m_patches;
			m_patches.insert({m_part, patch});
		}
		return m_patches;
	}

	bool SavePatchDesc::writePatchesToFile(const juce::File& _file) const
	{
		const auto& patches = getPatches();

		if(patches.empty())
			return false;

		std::vector<std::vector<uint8_t>> patchesData;

		for (auto& patch : patches)
		{
			auto data = m_patchManager.prepareSave(patch.second);
			if(data.empty())
				return false;
			patchesData.emplace_back(std::move(data));
		}

		std::stringstream ss;

		synthLib::SysexToMidi::write(ss, patchesData);

		const auto size = ss.tellp();
		std::vector<char> buffer;
		buffer.resize(size);
		ss.read(buffer.data(), size);

		return _file.replaceWithData(buffer.data(), size);
	}

	std::string getPatchName(const pluginLib::patchDB::PatchPtr& _patch)
	{
		auto name = _patch->getName();

		// trim whitespace
		while(!name.empty() && name.back() == ' ')
			name.pop_back();

		return name;
	}

	std::string createValidFilename(const std::string& _name)
	{
		std::string result;
		result.reserve(_name.size());

		const std::string invalid = "\\/:?\"<>|";

		for (const char c : _name)
		{
			if(invalid.find(c) != std::string::npos)
				result += '_';
			else
				result += c;
		}
		return result;
	}

	std::string SavePatchDesc::getExportFileName(const pluginLib::Processor& _p) const
	{
		const auto& patches = getPatches();

		std::string name;

		if(patches.size() == 1)
		{
			name = getPatchName(patches.begin()->second);		// if there is only one patch, we use the name of the patch
		}
		else if(!m_name.empty())
		{
			name = m_name;									// otherwise use custom name if provided
		}
		else
		{
			name = std::to_string(patches.size()) + " patches";
		}

		return _p.getProperties().name + " - " + createValidFilename(name) + ".mid";
	}

	bool SavePatchDesc::writeToFile(const juce::File& _file) const
	{
		return writePatchesToFile(_file);
	}

	std::vector<pluginLib::patchDB::PatchPtr> SavePatchDesc::getPatchesFromDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		const auto* savePatchDesc = fromDragSource(_dragSourceDetails);

		if(!savePatchDesc)
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& it : savePatchDesc->getPatches())
			patches.push_back(it.second);

		return patches;
	}
}
