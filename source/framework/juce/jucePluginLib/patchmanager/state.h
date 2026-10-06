#pragma once

#include <array>

#include "jucePluginLib/patchdb/patch.h"
#include "jucePluginLib/types.h"

namespace pluginLib::patchDB
{
	class DB;
}

namespace pluginLib::patchManager
{
	// the order in which a list of patches is shown and browsed: folders by file, files, ROMs and user banks by program
	void sortPatches(std::vector<patchDB::PatchPtr>& _patches, patchDB::SourceType _sourceType);

	class PartState
	{
	public:
		void setSelectedPatch(const patchDB::PatchKey& _patch, uint32_t _searchHandle);

		const auto& getPatch() const { return m_patch; }
		const auto& getSearchHandle() const { return m_searchHandle; }

		bool isValid() const { return m_patch.isValid() && m_searchHandle != patchDB::g_invalidSearchHandle; }

		void setConfig(PluginStream& _s);
		void getConfig(PluginStream& _s) const;

		void clear();

	private:
		patchDB::PatchKey m_patch;
		patchDB::SearchHandle m_searchHandle = patchDB::g_invalidSearchHandle;
	};

	class State
	{
	public:
		explicit State(patchDB::DB& _db) : m_db(_db), m_parts({})
		{
		}

		void setSelectedPatch(const uint32_t _part, const patchDB::PatchKey& _patch, uint32_t _searchHandle);

		std::pair<patchDB::PatchPtr, uint32_t> getNeighbourPreset(uint32_t _part, int _offset) const;
		std::pair<patchDB::PatchPtr, uint32_t> getNeighbourPreset(const patchDB::PatchKey& _patch, patchDB::SearchHandle _searchHandle, int _offset) const;

		patchDB::PatchKey getPatch(uint32_t _part) const;
		patchDB::SearchHandle getSearchHandle(uint32_t _part) const;

		bool isValid(uint32_t _part) const;

		std::pair<std::vector<patchDB::PatchPtr>, uint32_t> getPatchesAndIndex(const patchDB::PatchKey& _patch, patchDB::SearchHandle _searchHandle) const;

		void setConfig(PluginStream& _s);
		void getConfig(PluginStream& _s) const;

		uint32_t getPartCount() const
		{
			return static_cast<uint32_t>(m_parts.size());
		}

		void clear(const uint32_t _part);
		void copy(uint8_t _target, uint8_t _source);

	private:
		patchDB::DB& m_db;
		std::array<PartState, 16> m_parts;
	};
}
