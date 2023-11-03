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

	void TypedTags::add(TagType _type, const Tag& _tag)
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
}
