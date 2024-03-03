#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace synthLib
{
	class MD5
	{
	public:
		MD5(const std::vector<uint8_t>& _data);

		std::string toString() const;

	private:
		std::array<uint32_t, 4> m_h;
	};
}
