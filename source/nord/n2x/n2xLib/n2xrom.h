#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "n2xtypes.h"

namespace n2x
{
	class Rom
	{
	public:
		Rom();

		bool isValid() const { return !m_filename.empty(); }
		const auto& data() const { return m_data; }

	private:
		std::array<uint8_t, g_romSize> m_data;
		std::string m_filename;
	};
}
