#include "tags.h"

namespace pluginLib::patchDB
{
	const Tags& TypedTags::get(const TagType _type) const
	{
		const auto& it = m_tags.find(_type);
		if (it != m_tags.end())
			return it->second;
		static Tags empty;
		return empty;
	}

	bool TypedTags::add(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if(it == m_tags.end())
		{
			Tags t;
			t.add(_tag);
			m_tags.insert({ _type, t});
			return true;
		}

		return it->second.add(_tag);
	}

	bool TypedTags::addRemoved(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if (it == m_tags.end())
		{
			Tags t;
			t.addRemoved(_tag);
			m_tags.insert({ _type, t });
			return true;
		}

		return it->second.addRemoved(_tag);
	}

	bool TypedTags::erase(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if (it == m_tags.end())
			return false;

		if (!it->second.erase(_tag))
			return false;

		if (it->second.empty())
			m_tags.erase(_type);

		return true;
	}

	bool TypedTags::containsAdded(const TagType _type, const Tag& _tag) const
	{
		const auto itType = m_tags.find(_type);
		if (itType == m_tags.end())
			return false;
		return itType->second.containsAdded(_tag);
	}

	bool TypedTags::containsRemoved(const TagType _type, const Tag& _tag) const
	{
		const auto itType = m_tags.find(_type);
		if (itType == m_tags.end())
			return false;
		return itType->second.containsRemoved(_tag);
	}

	void TypedTags::clear()
	{
		m_tags.clear();
	}
}
