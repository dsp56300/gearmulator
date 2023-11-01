#pragma once

#include "tags.h"

namespace pluginLib::patchDB
{
	struct PatchModifications
	{
		Tags tags;
		Tags categories;
		std::string name;
	};
}
