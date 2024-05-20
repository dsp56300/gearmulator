#include "savepatchdesc.h"

#include "patchmanager.h"

#include "../../synthLib/sysexToMidi.h"

namespace jucePluginEditorLib::patchManager
{
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
