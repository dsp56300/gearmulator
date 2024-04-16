#pragma once

#include <array>

#include "../../jucePluginLib/patchdb/patch.h"
#include "../../jucePluginLib/types.h"

namespace jucePluginEditorLib::patchManager
{
	class State;
	class PatchManager;

	class PartState
	{
	public:
		void setSelectedPatch(const pluginLib::patchDB::PatchKey& _patch, uint32_t _searchHandle);

		const auto& getPatch() const { return m_patch; }
		const auto& getSearchHandle() const { return m_searchHandle; }

		bool isValid() const { return m_patch.isValid() && m_searchHandle != pluginLib::patchDB::g_invalidSearchHandle; }

		void setConfig(pluginLib::PluginStream& _s);
		void getConfig(pluginLib::PluginStream& _s) const;

		void clear();

	private:
		pluginLib::patchDB::PatchKey m_patch;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	};

	class State
	{
	public:
		explicit State(PatchManager& _patchManager) : m_patchManager(_patchManager), m_parts({})
		{
		}

		void setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchKey& _patch, uint32_t _searchHandle);

		std::pair<pluginLib::patchDB::PatchPtr, uint32_t> getNeighbourPreset(uint32_t _part, int _offset) const;
		std::pair<pluginLib::patchDB::PatchPtr, uint32_t> getNeighbourPreset(const pluginLib::patchDB::PatchKey& _patch, pluginLib::patchDB::SearchHandle _searchHandle, int _offset) const;

		pluginLib::patchDB::PatchKey getPatch(uint32_t _part) const;
		pluginLib::patchDB::SearchHandle getSearchHandle(uint32_t _part) const;

		bool isValid(uint32_t _part) const;

		std::pair<std::vector<pluginLib::patchDB::PatchPtr>, uint32_t> getPatchesAndIndex(const pluginLib::patchDB::PatchKey& _patch, pluginLib::patchDB::SearchHandle _searchHandle) const;

		void setConfig(pluginLib::PluginStream& _s);
		void getConfig(pluginLib::PluginStream& _s) const;

		uint32_t getPartCount() const
		{
			return static_cast<uint32_t>(m_parts.size());
		}

		void clear(const uint32_t _part);
		void copy(uint8_t _target, uint8_t _source);

	private:
		PatchManager& m_patchManager;
		std::array<PartState, 16> m_parts;
	};
}
