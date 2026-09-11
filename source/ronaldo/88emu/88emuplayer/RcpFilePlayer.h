#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sc88smf
{
	struct Event;
}

namespace rcpfile
{
	bool isRcpV2(const std::vector<std::uint8_t>& data) noexcept;
	bool loadRcpV2(const std::vector<std::uint8_t>& data, std::vector<sc88smf::Event>& destination, std::string& error);
} // namespace rcpfile
