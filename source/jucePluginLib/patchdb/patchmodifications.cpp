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

			for (const auto& tag : t.getAdded())
			{
				if (!patch->tags.containsAdded(type, tag))
					res |= tags.add(type, tag);
			}

			for (const auto& tag : t.getRemoved())
			{
				if (patch->tags.containsAdded(type, tag))
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
		mergedTags.clear();

		if (!patch)
			return;

		mergedTags = patch->tags;

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

		auto* doTags = new juce::DynamicObject();

		for (const auto& it : tags.get())
		{
			const auto& key = it.first;
			const auto& tags = it.second;

			auto* doType = new juce::DynamicObject();

			const auto& added = tags.getAdded();
			const auto& removed = tags.getRemoved();

			if(!added.empty())
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

		o->setProperty("tags", doTags);

		if(!name.empty())
		{
			if (!patch || name != patch->name)
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
			for(const auto& prop : t->getProperties())
			{
				const auto type = toTagType(prop.name.toString().toStdString());

				if(type == TagType::Invalid)
					continue;

				const auto* added = prop.value["added"].getArray();
				const auto* removed = prop.value["removed"].getArray();

				if(added)
				{
					for (const auto& var : *added)
					{
						const auto& tag = var.toString().toStdString();
						if(!tag.empty())
							tags.add(type, tag);
					}
				}

				if (removed)
				{
					for (const auto& var : *removed)
					{
						const auto& tag = var.toString().toStdString();
						if (!tag.empty())
							tags.addRemoved(type, tag);
					}
				}
			}
		}

		return true;
	}
}
