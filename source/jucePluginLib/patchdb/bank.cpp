#include "bank.h"

#include "patch.h"

namespace pluginLib::patchDB
{
	bool Bank::movePatchesTo(uint32_t _position, const std::vector<PatchPtr>& _patches)
	{
		if(!remove(_patches))
			return false;

		for(uint32_t i=0; i<static_cast<uint32_t>(_patches.size()); ++i)
			m_patches.insert(m_patches.begin() + _position + i, _patches[i]);

		return true;
	}

	bool Bank::contains(const std::vector<PatchPtr>& _patches) const
	{
		for (const auto& patch : _patches)
		{
			if(!contains(patch))
				return false;
		}
		return true;
	}

	bool Bank::contains(const PatchPtr& _patch) const
	{
		return std::find(m_patches.begin(), m_patches.end(), _patch) != m_patches.end();
	}

	bool Bank::remove(const std::vector<PatchPtr>& _patches)
	{
		if(!contains(_patches))
			return false;

		for (const auto & patch : _patches)
			m_patches.erase(std::find(m_patches.begin(), m_patches.end(), patch));

		return true;
	}

	bool Bank::remove(const PatchPtr& _patch)
	{
		const auto it = std::find(m_patches.begin(), m_patches.end(), _patch);
		if(it == m_patches.end())
			return false;
		m_patches.erase(it);
		return true;
	}

	void Bank::updateIndices() const
	{
		for(uint32_t i=0; i<m_patches.size(); ++i)
		{
			const auto& p = m_patches[i];
			p->program = i;
		}
	}
}
