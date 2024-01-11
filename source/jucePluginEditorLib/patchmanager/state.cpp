#include "state.h"

#include "list.h"
#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	void PartState::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _searchHandle, uint32_t _index)
	{
		m_patch = _patch;
		m_searchHandle = _searchHandle;
		m_index = _index;
	}

	void State::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _searchHandle, uint32_t _indexInSearch)
	{
		if(_part >= static_cast<int>(m_parts.size()))
			return;

		const auto& search = m_patchManager.getSearch(_searchHandle);

		if(_indexInSearch == pluginLib::patchDB::g_invalidProgram)
		{
			std::vector<pluginLib::patchDB::PatchPtr> patches;

			{
				std::shared_lock lock(search->resultsMutex);
				patches.assign(search->results.begin(), search->results.end());
			}

			List::sortPatches(patches, search->getSourceType());

			const auto it = std::find(patches.begin(), patches.end(), _patch);

			if(it != patches.end())
				_indexInSearch = static_cast<uint32_t>(it - patches.begin());
		}

		m_parts[_part].setSelectedPatch(_patch, _searchHandle, _indexInSearch);
	}
}
