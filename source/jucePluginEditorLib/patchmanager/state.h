#pragma once

#include <array>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"
#include "../../jucePluginLib/types.h"

namespace jucePluginEditorLib::patchManager
{
	class State;
	class PatchManager;

	class PartState
	{
	public:
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _searchHandle, uint32_t _index);

		const auto& getPatch() const { return m_patch; }
		const auto& getSearchHandle() const { return m_searchHandle; }
		const auto& getIndex() const { return m_searchHandle; }

		bool isValid() const { return m_patch && m_searchHandle != pluginLib::patchDB::g_invalidSearchHandle && m_index != pluginLib::patchDB::g_invalidProgram;}

		void setConfig(pluginLib::PluginStream& _s);
		void getConfig(pluginLib::PluginStream& _s);

	private:
		pluginLib::patchDB::PatchPtr m_patch;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
		uint32_t m_index = pluginLib::patchDB::g_invalidProgram;
	};

	class State
	{
	public:
		explicit State(PatchManager& _patchManager) : m_patchManager(_patchManager), m_parts({})
		{
		}

		void setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, uint32_t _searchHandle, uint32_t _index = pluginLib::patchDB::g_invalidProgram);
		std::pair<pluginLib::patchDB::PatchPtr, uint32_t> getNeighbourPreset(uint32_t _part, int _offset) const;
		std::pair<pluginLib::patchDB::PatchPtr, uint32_t> getNeighbourPreset(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _searchHandle, int _offset) const;

		pluginLib::patchDB::PatchPtr getPatch(uint32_t _part) const;
		pluginLib::patchDB::SearchHandle getSearchHandle(uint32_t _part) const;
		uint32_t getIndex(uint32_t _part) const;

		bool isValid(uint32_t _part) const;

		std::pair<std::vector<pluginLib::patchDB::PatchPtr>, uint32_t> getPatchesAndIndex(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _searchHandle) const;

		void setConfig(pluginLib::PluginStream& _s);
		void getConfig(pluginLib::PluginStream& _s);

	private:
		PatchManager& m_patchManager;
		std::array<PartState, 16> m_parts;
	};
}
