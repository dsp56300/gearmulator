#pragma once

#include <array>

#include "search.h"

namespace pluginLib::patchDB
{
	class PartState
	{
	public:
		SearchHandle searchHandle = g_invalidSearchHandle;
		uint32_t index = g_invalidProgram;
	};

	class State
	{
	public:
		std::array<PartState, 16>;
	};
}
