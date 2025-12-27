#pragma once

#include <cstdint>

namespace jeJucePlugin
{
	enum class PatchType : uint8_t
	{
		PartUpper,
		PartLower,
		Performance,

		Count
	};

	enum class JePart
	{
		PatchUpper = 0,
		PatchLower = 1,
		Performance = 2
	};
}
