#pragma once

#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	class Bank
	{
	public:
		Bank() = default;

		auto empty() const { return m_patches.empty(); }
		auto size() const { return m_patches.size(); }

		const auto& getPatches() const { return m_patches; }

		bool movePatchesTo(uint32_t _position, const std::vector<PatchPtr>& _patches);

		bool contains(const std::vector<PatchPtr>& _patches) const;
		bool contains(const PatchPtr& _patch) const;

		bool remove(const std::vector<PatchPtr>& _patches);
		bool remove(const PatchPtr& _patch);

	private:
		void updateIndices() const;

		std::vector<PatchPtr> m_patches;
	};
}
