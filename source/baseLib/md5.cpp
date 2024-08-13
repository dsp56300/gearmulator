#include "md5.h"

#include <cstring>	// memcpy
#include <iomanip>
#include <sstream>

#define HEXN(S, n)		std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S

namespace baseLib
{
	// leftrotate function definition
	uint32_t leftrotate(const uint32_t x, const uint32_t c)
	{
		return (((x) << (c)) | ((x) >> (32 - (c))));
	}

	// These vars will contain the hash: h0, h1, h2, h3
	void md5(uint32_t& _h0, uint32_t& _h1, uint32_t& _h2, uint32_t& _h3, const uint8_t *_initialMsg, const uint32_t _initialLen)
	{
		// Note: All variables are unsigned 32 bit and wrap modulo 2^32 when calculating

		// r specifies the per-round shift amounts

		constexpr uint32_t r[] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
								  5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
								  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
								  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

		// Use binary integer part of the sines of integers (in radians) as constants// Initialize variables:
		constexpr uint32_t k[] = {
			0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
			0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
			0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
			0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
			0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
			0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
			0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
			0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
			0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
			0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
			0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
			0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
			0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
			0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
			0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
			0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

		_h0 = 0x67452301;
		_h1 = 0xefcdab89;
		_h2 = 0x98badcfe;
		_h3 = 0x10325476;

		// Pre-processing: adding a single 1 bit
		//append "1" bit to message    
		/* Notice: the input bytes are considered as bits strings,
		   where the first bit is the most significant bit of the byte.[37] */

		   // Pre-processing: padding with zeros
		   //append "0" bit until message length in bit ≡ 448 (mod 512)
		   //append length mod (2 pow 64) to message

		const uint32_t newLen = ((((_initialLen + 8) / 64) + 1) * 64) - 8;

		std::vector<uint8_t> buffer;
		buffer.resize(newLen + 64, 0);
		auto* msg = buffer.data();
		memcpy(msg, _initialMsg, _initialLen);
		msg[_initialLen] = 128; // write the "1" bit

		const uint32_t bitsLen = 8 * _initialLen; // note, we append the len
		memcpy(msg + newLen, &bitsLen, 4);           // in bits at the end of the buffer

		// Process the message in successive 512-bit chunks:
		//for each 512-bit chunk of message:
		for (uint32_t offset = 0; offset < newLen; offset += (512 / 8))
		{
			// break chunk into sixteen 32-bit words w[j], 0 ≤ j ≤ 15
			const auto* w = reinterpret_cast<uint32_t*>(msg + offset);

			// Initialize hash value for this chunk:
			uint32_t a = _h0;
			uint32_t b = _h1;
			uint32_t c = _h2;
			uint32_t d = _h3;

			// Main loop:
			for (uint32_t i = 0; i < 64; i++)
			{
				uint32_t f, g;

				if (i < 16) {
					f = (b & c) | ((~b) & d);
					g = i;
				}
				else if (i < 32) {
					f = (d & b) | ((~d) & c);
					g = (5 * i + 1) % 16;
				}
				else if (i < 48) {
					f = b ^ c ^ d;
					g = (3 * i + 5) % 16;
				}
				else {
					f = c ^ (b | (~d));
					g = (7 * i) % 16;
				}

				const uint32_t temp = d;
				d = c;
				c = b;
//				printf("rotateLeft(%x + %x + %x + %x, %d)\n", a, f, k[i], w[g], r[i]);
				b = b + leftrotate((a + f + k[i] + w[g]), r[i]);
				a = temp;
			}

			// Add this chunk's hash to result so far:

			_h0 += a;
			_h1 += b;
			_h2 += c;
			_h3 += d;

		}

		// cleanup
	//    free(msg); 
	}

	MD5::MD5(const std::vector<uint8_t>& _data)
	{
		md5(m_h[0], m_h[1], m_h[2], m_h[3], _data.data(), static_cast<uint32_t>(_data.size()));
	}

	std::string MD5::toString() const
	{
		std::stringstream ss;

		for (const auto& e : m_h)
		{
			ss << HEXN((e & 0xff), 2);
			ss << HEXN(((e>>8u) & 0xff), 2);
			ss << HEXN(((e>>16u) & 0xff), 2);
			ss << HEXN(((e>>24u) & 0xff), 2);
		}
		return ss.str();
	}
}
