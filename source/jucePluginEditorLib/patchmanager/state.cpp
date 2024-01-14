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

	void PartState::setConfig(pluginLib::PluginStream& _s)
	{
	}

	void PartState::getConfig(pluginLib::PluginStream& _s)
	{
	}

	void State::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _searchHandle, uint32_t _indexInSearch)
	{
		if(_part >= static_cast<int>(m_parts.size()))
			return;

		if (_indexInSearch == pluginLib::patchDB::g_invalidProgram)
		{
			const auto result = getPatchesAndIndex(_patch, _searchHandle);

			const auto index = result.second;

			if(index != pluginLib::patchDB::g_invalidProgram)
				_indexInSearch = index;
		}

		m_parts[_part].setSelectedPatch(_patch, _searchHandle, _indexInSearch);
	}

	std::pair<pluginLib::patchDB::PatchPtr, uint32_t> State::getNeighbourPreset(const uint32_t _part, const int _offset) const
	{
		if(_part >= m_parts.size())
			return {nullptr, pluginLib::patchDB::g_invalidProgram};

		const auto& part = m_parts[_part];

		return getNeighbourPreset(part.getPatch(), part.getSearchHandle(), _offset);
	}

	std::pair<pluginLib::patchDB::PatchPtr, uint32_t> State::getNeighbourPreset(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _searchHandle, const int _offset) const
	{
		const auto result = getPatchesAndIndex(_patch, _searchHandle);

		const auto& patches = result.first;
		const auto index = result.second;

		if(index == pluginLib::patchDB::g_invalidProgram)
			return {nullptr, pluginLib::patchDB::g_invalidProgram};

		if(patches.size() <= 1)
			return {nullptr, pluginLib::patchDB::g_invalidProgram};

		const auto count = static_cast<int>(patches.size());
		auto i = static_cast<int>(index) + _offset;

		if(i < 0)
			i += count;
		if(i >= count)
			i -= count;

		return {patches[i], i};
	}

	pluginLib::patchDB::PatchPtr State::getPatch(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return nullptr;
		return m_parts[_part].getPatch();
	}

	pluginLib::patchDB::SearchHandle State::getSearchHandle(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_parts[_part].getSearchHandle();
	}

	uint32_t State::getIndex(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return pluginLib::patchDB::g_invalidProgram;
		return m_parts[_part].getIndex();
	}

	bool State::isValid(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return false;
		return m_parts[_part].isValid();
	}

	std::pair<std::vector<pluginLib::patchDB::PatchPtr>, uint32_t> State::getPatchesAndIndex(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _searchHandle) const
	{
		const auto& search = m_patchManager.getSearch(_searchHandle);

		if(!search)
			return {{}, pluginLib::patchDB::g_invalidProgram};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		{
			std::shared_lock lock(search->resultsMutex);
			patches.assign(search->results.begin(), search->results.end());
		}

		List::sortPatches(patches, search->getSourceType());

		const auto it = std::find(patches.begin(), patches.end(), _patch);

		auto index = pluginLib::patchDB::g_invalidProgram;

		if(it != patches.end())
			index = static_cast<uint32_t>(it - patches.begin());

		return {patches, index};
	}

	void State::setConfig(pluginLib::PluginStream& _s)
	{
		const auto version = _s.read<uint32_t>();
		if(version != 1)
			return;

		const auto numParts = _s.read<uint32_t>();

		for(size_t i=0; i<numParts; ++i)
		{
			if(i < m_parts.size())
			{
				m_parts[i].setConfig(_s);
			}
			else
			{
				PartState unused;
				unused.setConfig(_s);
			}
		}
	}

	void State::getConfig(pluginLib::PluginStream& _s)
	{
	}
}
