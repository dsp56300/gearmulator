#pragma once

#include <array>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	class State;
	class PatchManager;

	class PartState
	{
	public:
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _searchHandle, uint32_t _index);

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

	private:
		PatchManager& m_patchManager;
		std::array<PartState, 16> m_parts;
	};
}
