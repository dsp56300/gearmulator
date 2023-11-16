#include "tags.h"

#include "juce_core/juce_core.h"

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

	bool TypedTags::empty() const
	{
		for (const auto& tag : m_tags)
		{
			if (!tag.second.empty())
				return false;
		}
		return true;
	}

	juce::DynamicObject* TypedTags::serialize() const
	{
		auto* doTags = new juce::DynamicObject();

		for (const auto& it : get())
		{
			const auto& key = it.first;
			const auto& tags = it.second;

			auto* doType = new juce::DynamicObject();

			const auto& added = tags.getAdded();
			const auto& removed = tags.getRemoved();

			if (!added.empty())
			{
				juce::Array<juce::var> doAdded;
				for (const auto& tag : added)
					doAdded.add(juce::String(tag));
				doType->setProperty("added", doAdded);
			}

			if (!removed.empty())
			{
				juce::Array<juce::var> doRemoved;
				for (const auto& tag : removed)
					doRemoved.add(juce::String(tag));
				doType->setProperty("removed", doRemoved);
			}

			doTags->setProperty(juce::String(toString(key)), doType);
		}

		return doTags;
	}

	void TypedTags::deserialize(juce::DynamicObject* _obj)
	{
		for (const auto& prop : _obj->getProperties())
		{
			const auto type = toTagType(prop.name.toString().toStdString());

			if (type == TagType::Invalid)
				continue;

			const auto* added = prop.value["added"].getArray();
			const auto* removed = prop.value["removed"].getArray();

			if (added)
			{
				for (const auto& var : *added)
				{
					const auto& tag = var.toString().toStdString();
					if (!tag.empty())
						add(type, tag);
				}
			}

			if (removed)
			{
				for (const auto& var : *removed)
				{
					const auto& tag = var.toString().toStdString();
					if (!tag.empty())
						addRemoved(type, tag);
				}
			}
		}
	}
}
