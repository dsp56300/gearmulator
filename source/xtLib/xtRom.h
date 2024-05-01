#pragma once

#include "xtTypes.h"

#include "../wLib/wRom.h"

namespace xt
{
	class Rom : public wLib::ROM
	{
	public:
		static constexpr uint32_t Size = g_romSize;

		Rom(const std::string& _filename) : ROM(_filename, Size)
		{
		}

		uint32_t getSize() const override
		{
			return Size;
		}
	};
}
