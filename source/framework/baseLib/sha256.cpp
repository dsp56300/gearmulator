#include "sha256.h"

#include <cstring>	// memcpy
#include <iomanip>
#include <sstream>

namespace baseLib
{
	namespace
	{
		constexpr uint32_t rightRotate(const uint32_t _x, const uint32_t _c)
		{
			return (_x >> _c) | (_x << (32 - _c));
		}

		constexpr uint32_t g_k[64] =
		{
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		void processChunk(std::array<uint32_t, 8>& _h, const uint8_t* _chunk)
		{
			uint32_t w[64];

			for (uint32_t i = 0; i < 16; ++i)
			{
				w[i] = static_cast<uint32_t>(_chunk[i * 4    ]) << 24 |
				       static_cast<uint32_t>(_chunk[i * 4 + 1]) << 16 |
				       static_cast<uint32_t>(_chunk[i * 4 + 2]) <<  8 |
				       static_cast<uint32_t>(_chunk[i * 4 + 3]);
			}

			for (uint32_t i = 16; i < 64; ++i)
			{
				const uint32_t s0 = rightRotate(w[i - 15], 7) ^ rightRotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const uint32_t s1 = rightRotate(w[i - 2], 17) ^ rightRotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}

			uint32_t a = _h[0];
			uint32_t b = _h[1];
			uint32_t c = _h[2];
			uint32_t d = _h[3];
			uint32_t e = _h[4];
			uint32_t f = _h[5];
			uint32_t g = _h[6];
			uint32_t h = _h[7];

			for (uint32_t i = 0; i < 64; ++i)
			{
				const uint32_t s1 = rightRotate(e, 6) ^ rightRotate(e, 11) ^ rightRotate(e, 25);
				const uint32_t ch = (e & f) ^ (~e & g);
				const uint32_t temp1 = h + s1 + ch + g_k[i] + w[i];
				const uint32_t s0 = rightRotate(a, 2) ^ rightRotate(a, 13) ^ rightRotate(a, 22);
				const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
				const uint32_t temp2 = s0 + maj;

				h = g;
				g = f;
				f = e;
				e = d + temp1;
				d = c;
				c = b;
				b = a;
				a = temp1 + temp2;
			}

			_h[0] += a;
			_h[1] += b;
			_h[2] += c;
			_h[3] += d;
			_h[4] += e;
			_h[5] += f;
			_h[6] += g;
			_h[7] += h;
		}
	}

	SHA256::SHA256(const std::vector<uint8_t>& _data) : SHA256(_data.data(), _data.size())
	{
	}

	// Chunks are taken straight out of the caller's buffer; only the tail is copied, as
	// SHA1 does it.
	SHA256::SHA256(const uint8_t* _data, const size_t _size)
	: m_h({0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19})
	{
		constexpr size_t chunkSize = 64;

		size_t offset = 0;
		for (; offset + chunkSize <= _size; offset += chunkSize)
			processChunk(m_h, _data + offset);

		// The terminator bit and the 64-bit big-endian message length only fit alongside a
		// remainder shorter than 56 bytes; anything longer needs a second chunk.
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

	std::string SHA256::toString() const
	{
		std::stringstream ss;

		for (const auto& e : m_h)
			ss << std::hex << std::setfill('0') << std::setw(8) << e;

		return ss.str();
	}
}
