#include "patchmodifications.h"

#include "patch.h"

namespace pluginLib::patchDB
{
	void PatchModifications::modifyTags(const TypedTags& _tags)
	{
		for (const auto& it : _tags.get())
		{
			const auto type = it.first;
			const auto& t = it.second;

			for (const auto& tag : t.getAdded())
			{
				if (!patch->tags.containsAdded(type, tag))
					tags.add(type, tag);
			}

			for (const auto& tag : t.getRemoved())
			{
				if (patch->tags.containsAdded(type, tag))
					tags.addRemoved(type, tag);
				else if (tags.containsAdded(type, tag))
					tags.erase(type, tag);
			}
		}

		updateCache();
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
}
