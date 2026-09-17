#include "sha1.h"

#include <cstring>	// memcpy
#include <iomanip>
#include <sstream>

namespace baseLib
{
	namespace
	{
		constexpr uint32_t leftRotate(const uint32_t _x, const uint32_t _c)
		{
			return (_x << _c) | (_x >> (32 - _c));
		}

		void processChunk(std::array<uint32_t, 5>& _h, const uint8_t* _chunk)
		{
			uint32_t w[80];

			for (uint32_t i = 0; i < 16; ++i)
			{
				w[i] = static_cast<uint32_t>(_chunk[i * 4    ]) << 24 |
				       static_cast<uint32_t>(_chunk[i * 4 + 1]) << 16 |
				       static_cast<uint32_t>(_chunk[i * 4 + 2]) <<  8 |
				       static_cast<uint32_t>(_chunk[i * 4 + 3]);
			}

			for (uint32_t i = 16; i < 80; ++i)
				w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

			uint32_t a = _h[0];
			uint32_t b = _h[1];
			uint32_t c = _h[2];
			uint32_t d = _h[3];
			uint32_t e = _h[4];

			for (uint32_t i = 0; i < 80; ++i)
			{
				uint32_t f, k;

				if (i < 20)
				{
					f = (b & c) | (~b & d);
					k = 0x5a827999;
				}
				else if (i < 40)
				{
					f = b ^ c ^ d;
					k = 0x6ed9eba1;
				}
				else if (i < 60)
				{
					f = (b & c) | (b & d) | (c & d);
					k = 0x8f1bbcdc;
				}
				else
				{
					f = b ^ c ^ d;
					k = 0xca62c1d6;
				}

				const uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
				e = d;
				d = c;
				c = leftRotate(b, 30);
				b = a;
				a = temp;
			}

			_h[0] += a;
			_h[1] += b;
			_h[2] += c;
			_h[3] += d;
			_h[4] += e;
		}
	}

	SHA1::SHA1(const std::vector<uint8_t>& _data) : SHA1(_data.data(), _data.size())
	{
	}

	// Chunks are taken straight out of the caller's buffer; only the tail is
	// copied, so hashing a 32 MiB wave dump does not duplicate it.
	SHA1::SHA1(const uint8_t* _data, const size_t _size)
	: m_h({0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0})
	{
		constexpr size_t chunkSize = 64;

		size_t offset = 0;
		for (; offset + chunkSize <= _size; offset += chunkSize)
			processChunk(m_h, _data + offset);

		// Append the terminator bit and the 64-bit big-endian message length. Both
		// only fit alongside a remainder shorter than 56 bytes; anything longer
		// needs a second chunk.
		const auto remaining = _size - offset;
		uint8_t tail[chunkSize * 2] = {};
		if (remaining)
			memcpy(tail, _data + offset, remaining);
		tail[remaining] = 0x80;

		const size_t tailSize = remaining >= chunkSize - 8 ? chunkSize * 2 : chunkSize;
		const uint64_t bits = static_cast<uint64_t>(_size) * 8;
		for (size_t i = 0; i < 8; ++i)
			tail[tailSize - 1 - i] = static_cast<uint8_t>(bits >> (i * 8));

		processChunk(m_h, tail);
		if (tailSize > chunkSize)
			processChunk(m_h, tail + chunkSize);
	}

	std::string SHA1::toString() const
	{
		std::stringstream ss;

		for (const auto& e : m_h)
			ss << std::hex << std::setfill('0') << std::setw(8) << e;

		return ss.str();
	}
}
