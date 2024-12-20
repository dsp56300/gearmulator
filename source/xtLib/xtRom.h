#pragma once

#include <utility>

#include "xtTypes.h"

#include "wLib/wRom.h"

namespace xt
{
	class Rom : public wLib::ROM
	{
	public:
		static constexpr uint32_t Size = g_romSize;

		Rom(const std::string& _filename, std::vector<uint8_t> _data) : ROM(_filename, Size, std::move(_data))
		{
		}

		uint32_t getSize() const override
		{
			return Size;
		}

		static Rom invalid()
		{
			return {{}, {}};
		}
	};
}
