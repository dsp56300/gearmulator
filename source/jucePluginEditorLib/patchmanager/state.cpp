#include "state.h"

#include "list.h"
#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	void PartState::setSelectedPatch(const pluginLib::patchDB::PatchKey& _patch, uint32_t _searchHandle)
	{
		m_patch = _patch;
		m_searchHandle = _searchHandle;
	}

	void PartState::setConfig(pluginLib::PluginStream& _s)
	{
		const auto patchKey = _s.readString();
		m_patch = pluginLib::patchDB::PatchKey::fromString(patchKey);
	}

	void PartState::getConfig(pluginLib::PluginStream& _s) const
	{
		_s.write(m_patch.toString());
	}

	void PartState::clear()
	{
		m_patch = {};
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	void State::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchKey& _patch, const uint32_t _searchHandle)
	{
		if(_part >= m_parts.size())
			return;

		m_parts[_part].setSelectedPatch(_patch, _searchHandle);
	}

	std::pair<pluginLib::patchDB::PatchPtr, uint32_t> State::getNeighbourPreset(const uint32_t _part, const int _offset) const
	{
		if(_part >= m_parts.size())
			return {nullptr, pluginLib::patchDB::g_invalidProgram};

		const auto& part = m_parts[_part];

		return getNeighbourPreset(part.getPatch(), part.getSearchHandle(), _offset);
	}

	std::pair<pluginLib::patchDB::PatchPtr, uint32_t> State::getNeighbourPreset(const pluginLib::patchDB::PatchKey& _patch, const pluginLib::patchDB::SearchHandle _searchHandle, const int _offset) const
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

	pluginLib::patchDB::PatchKey State::getPatch(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return {};
		return m_parts[_part].getPatch();
	}

	pluginLib::patchDB::SearchHandle State::getSearchHandle(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_parts[_part].getSearchHandle();
	}

	bool State::isValid(const uint32_t _part) const
	{
		if(_part >= m_parts.size())
			return false;
		return m_parts[_part].isValid();
	}

	std::pair<std::vector<pluginLib::patchDB::PatchPtr>, uint32_t> State::getPatchesAndIndex(const pluginLib::patchDB::PatchKey& _patch, const pluginLib::patchDB::SearchHandle _searchHandle) const
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

		uint32_t index = pluginLib::patchDB::g_invalidProgram;

		for(uint32_t i=0; i<patches.size(); ++i)
		{
			if(pluginLib::patchDB::PatchKey(*patches[i]) == _patch)
			{
				index = i;
				break;
			}
		}

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

	void State::getConfig(pluginLib::PluginStream& _s) const
	{
		_s.write<uint32_t>(1);							// version
		_s.write<uint32_t>((uint32_t)m_parts.size());

		for (const auto& part : m_parts)
			part.getConfig(_s);
	}

	void State::clear(const uint32_t _part)
	{
		if(_part >= m_parts.size())
			return;
		m_parts[_part].clear();
	}

	void State::copy(const uint8_t _target, const uint8_t _source)
	{
		m_parts[_target] = m_parts[_source];
	}
}
