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

	void TypedTags::add(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if(it == m_tags.end())
		{
			Tags t;
			t.add(_tag);
			m_tags.insert({ _type, t});
		}
		else
		{
			it->second.add(_tag);
		}
	}

	void TypedTags::addRemoved(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if (it == m_tags.end())
		{
			Tags t;
			t.addRemoved(_tag);
			m_tags.insert({ _type, t });
		}
		else
		{
			it->second.addRemoved(_tag);
		}
	}

	void TypedTags::erase(const TagType _type, const Tag& _tag)
	{
		const auto it = m_tags.find(_type);

		if (it == m_tags.end())
			return;

		it->second.erase(_tag);

		if (it->second.empty())
			m_tags.erase(_type);
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
