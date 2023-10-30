#pragma once

#include <set>
#include <string>

namespace pluginLib::patchDB
{
	using Tag = std::string;

	class Tags
	{
	public:
		void add(const Tag& _tag)
		{
			m_added.insert(_tag);
			m_removed.erase(_tag);
		}

		void remove(const Tag& _tag)
		{
			m_added.erase(_tag);
			m_removed.insert(_tag);
		}

		const auto& get() const { return m_added; }

	private:
		std::set<Tag> m_added;
		std::set<Tag> m_removed;
	};
}
