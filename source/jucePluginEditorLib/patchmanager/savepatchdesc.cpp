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
