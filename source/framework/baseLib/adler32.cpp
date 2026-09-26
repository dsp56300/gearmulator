#include "adler32.h"

namespace baseLib
{
	uint32_t adler32(const uint8_t* _data, const size_t _size)
	{
		constexpr uint32_t modulo = 65521;

		uint32_t a = 1, b = 0;
		for(size_t i = 0; i < _size; ++i)
		{
			a = (a + _data[i]) % modulo;
			b = (b + a) % modulo;
		}
		return (b << 16) | a;
	}
}
