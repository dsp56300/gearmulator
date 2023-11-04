#pragma once

#include "tags.h"

namespace pluginLib::patchDB
{
	struct PatchModifications
	{
		void modifyTags(const TypedTags& _tags);
		void updateCache();

		PatchPtr patch;
		TypedTags tags;
		std::string name;

		// cache
		TypedTags mergedTags;
	};
}
