#include "rc4.h"

#include <array>
#include <utility>

namespace baseLib
{
	void rc4(uint8_t* _data, const size_t _size, const uint8_t* _key, const size_t _keySize)
	{
		if(!_keySize)
			return;

		std::array<uint8_t, 256> s{};
		for(size_t i = 0; i < s.size(); ++i)
			s[i] = static_cast<uint8_t>(i);

		uint8_t j = 0;
		for(size_t i = 0; i < s.size(); ++i)
		{
			j = static_cast<uint8_t>(j + s[i] + _key[i % _keySize]);
			std::swap(s[i], s[j]);
		}

		uint8_t a = 0;
		j = 0;
		for(size_t i = 0; i < _size; ++i)
		{
			++a;
			j = static_cast<uint8_t>(j + s[a]);
			std::swap(s[a], s[j]);
			_data[i] ^= s[static_cast<uint8_t>(s[a] + s[j])];
		}
	}
}
