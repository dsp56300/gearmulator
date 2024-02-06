#include "patchmodifications.h"

#include "patch.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace pluginLib::patchDB
{
	bool PatchModifications::modifyTags(const TypedTags& _tags)
	{
		bool res = false;

		for (const auto& it : _tags.get())
		{
			const auto type = it.first;
			const auto& t = it.second;

			const auto p = patch.lock();

			for (const auto& tag : t.getAdded())
			{
				if (!p->tags.containsAdded(type, tag))
					res |= tags.add(type, tag);
				else if(tags.containsRemoved(type, tag))
					res |= tags.erase(type, tag);
			}

			for (const auto& tag : t.getRemoved())
			{
				if (p->tags.containsAdded(type, tag))
					res |= tags.addRemoved(type, tag);
				else if (tags.containsAdded(type, tag))
					res |= tags.erase(type, tag);
			}
		}

		if (!res)
			return false;

		updateCache();
		return true;
	}

	void PatchModifications::updateCache()
	{
		const auto p = patch.lock();
		if (!p)
		{
			mergedTags = tags;
			return;
		}

		mergedTags = p->tags;

		for (const auto& it : tags.get())
		{
			const auto& type = it.first;
			const auto & t = it.second;

			for (const auto& tag: t.getAdded())
				mergedTags.add(type, tag);

			for (const auto& tag : t.getRemoved())
				mergedTags.addRemoved(type, tag);
		}
	}

	juce::DynamicObject* PatchModifications::serialize() const
	{
		auto* o = new juce::DynamicObject();

		auto* doTags = tags.serialize();

		o->setProperty("tags", doTags);

		if(!name.empty())
		{
			const auto p = patch.lock();
			if (!p || name != p->name)
				o->setProperty("name", juce::String(name));
		}

		return o;
	}

	bool PatchModifications::deserialize(const juce::var& _var)
	{
		name.clear();
		tags.clear();

		const auto n = _var["name"].toString();
		if (!n.isEmpty())
			name = n.toStdString();

		auto* t = _var["tags"].getDynamicObject();

		if(t)
		{
			tags.deserialize(t);
		}

		return true;
	}

	bool PatchModifications::empty() const
	{
		return name.empty() && tags.empty();
	}
}
