#include "patch.h"

#include "patchmodifications.h"

namespace pluginLib::patchDB
{
	const TypedTags& Patch::getTags() const
	{
		if (!modifications)
			return tags;
		return modifications->mergedTags;
	}

	const std::string& Patch::getName() const
	{
		if (!modifications)
			return name;
		if (modifications->name.empty())
			return name;
		return modifications->name;
	}
}
