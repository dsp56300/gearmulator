#pragma once

#include <cstdint>
#include <string>

namespace bridgeLib
{
	template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
	constexpr uint32_t cmd(const char (&_cmd)[N])
	{
		return (static_cast<uint32_t>(_cmd[0]) << 24) |
			(static_cast<uint32_t>(_cmd[1]) << 16) |
			(static_cast<uint32_t>(_cmd[2]) << 8) |
			(static_cast<uint32_t>(_cmd[3]));
	}
}
