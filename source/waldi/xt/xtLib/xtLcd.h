#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace xt
{
	class Lcd
	{
	public:
		Lcd();

		void resetWritePos();
		bool writeCharacter(char _c);
		const auto& getData() const { return m_lcdData; }
		std::string toString() const;

	private:
		uint16_t m_lcdWritePos = 0;
		std::array<char, 40*2> m_lcdData;
	};
}
