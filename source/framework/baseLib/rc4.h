#pragma once

#include <cstddef>
#include <cstdint>

namespace baseLib
{
	// RC4: XORs _data with the key stream of _key, which encrypts and decrypts alike. An empty key leaves _data alone
	void rc4(uint8_t* _data, size_t _size, const uint8_t* _key, size_t _keySize);
}
