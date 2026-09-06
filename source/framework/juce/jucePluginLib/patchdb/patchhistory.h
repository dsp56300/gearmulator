#pragma once

#include <vector>

#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	class PatchHistory
	{
		std::vector<PatchPtr> m_patches;
	};
}
