#include "tags.h"

#include "juce_core/juce_core.h"

#include "synthLib/binarystream.h"

namespace pluginLib::patchDB
{
	void Tags::write(synthLib::BinaryStream& _s) const
	{
		synthLib::ChunkWriter cw(_s, chunks::g_tags, 1);
		_s.write(m_added.size());
		for (const auto& added : m_added)
			_s.write(added);
		_s.write(m_removed.size());
		for (const auto& removed : m_removed)
			_s.write(removed);
	}

	bool Tags::read(synthLib::BinaryStream& _stream)
	{
		auto in = _stream.tryReadChunk(chunks::g_tags, 1);
		if(!in)
			return false;

		const auto numAdded = in.read<size_t>();

		for(size_t i=0; i<numAdded; ++i)
			m_added.insert(in.readString());

		const auto numRemoved = in.read<size_t>();

		for(size_t i=0; i<numRemoved; ++i)
			m_removed.insert(in.readString());

		return true;
	}

	bool Tags::operator==(const Tags& _t) const
	{
		if(m_added.size() != _t.m_added.size())
			return false;
		if(m_removed.size() != _t.m_removed.size())
			return false;

		for (const auto& e : m_added)
		{
			if(_t.m_added.find(e) == _t.m_added.end())
				return false;
		}

		for (const auto& e : m_removed)
		{
			if(_t.m_removed.find(e) == _t.m_removed.end())
				return false;
		}
		return true;
	}

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

	bool TypedTags::add(const TypedTags& _tags)
	{
		bool result = false;

		for (const auto& tags : _tags.get())
		{
			for (const auto& tag : tags.second.getAdded())
				result |= add(tags.first, tag);
		}

		return result;
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

	bool TypedTags::containsAdded() const
	{
		for (const auto& it : m_tags)
		{
			if(it.second.containsAdded())
				return true;
		}
		return false;
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

	bool TypedTags::operator==(const TypedTags& _tags) const
	{
		if(m_tags.size() != _tags.m_tags.size())
			return false;

		for (const auto& tags : m_tags)
		{
			const auto it = _tags.m_tags.find(tags.first);
			if(it == _tags.m_tags.end())
				return false;

			if(!(it->second == tags.second))
				return false;
		}
		return true;
	}

	void TypedTags::write(synthLib::BinaryStream& _s) const
	{
		synthLib::ChunkWriter cw(_s, chunks::g_typedTags, 1);
		_s.write(m_tags.size());

		for (const auto& t : m_tags)
		{
			_s.write(t.first);
			t.second.write(_s);
		}
	}

	bool TypedTags::read(synthLib::BinaryStream& _stream)
	{
		auto in = _stream.tryReadChunk(chunks::g_typedTags, 1);

		if(!in)
			return false;

		const auto count = in.read<size_t>();

		for(size_t i=0; i<count; ++i)
		{
			const auto type = in.read<TagType>();

			Tags t;

			if(!t.read(in))
				return false;

			m_tags.insert({type, t});
		}

		return true;
	}
}
