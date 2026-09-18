#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace baseLib
{
	class SHA1
	{
	public:
		static constexpr uint32_t parse1(const char _b)
		{
			if (_b >= '0' && _b <= '9')
				return _b - '0';
			if (_b >= 'A' && _b <= 'F')
				return _b - 'A' + 10;
			if (_b >= 'a' && _b <= 'f')
				return _b - 'a' + 10;
			assert(false);
			return 0;
		}

		// SHA-1 prints its five state words in big-endian order, so a digest string
		// reads straight into them, unlike MD5's little-endian layout.
		static constexpr uint32_t parse8(const char _b0, const char _b1, const char _b2, const char _b3, const char _b4, const char _b5, const char _b6, const char _b7)
		{
			return parse1(_b0) << 28 | parse1(_b1) << 24 | parse1(_b2) << 20 | parse1(_b3) << 16 |
			       parse1(_b4) << 12 | parse1(_b5) <<  8 | parse1(_b6) <<  4 | parse1(_b7);
		}

		template<size_t N, std::enable_if_t<N == 41, void*> = nullptr> constexpr SHA1(char const(&_digest)[N])
		: m_h
		{
			parse8(_digest[ 0], _digest[ 1], _digest[ 2], _digest[ 3], _digest[ 4], _digest[ 5], _digest[ 6], _digest[ 7]),
			parse8(_digest[ 8], _digest[ 9], _digest[10], _digest[11], _digest[12], _digest[13], _digest[14], _digest[15]),
			parse8(_digest[16], _digest[17], _digest[18], _digest[19], _digest[20], _digest[21], _digest[22], _digest[23]),
			parse8(_digest[24], _digest[25], _digest[26], _digest[27], _digest[28], _digest[29], _digest[30], _digest[31]),
			parse8(_digest[32], _digest[33], _digest[34], _digest[35], _digest[36], _digest[37], _digest[38], _digest[39])
		}
		{
		}

		explicit SHA1(const std::vector<uint8_t>& _data);
		explicit SHA1(const uint8_t* _data, size_t _size);

		constexpr SHA1() : m_h({0,0,0,0,0}) {}

		SHA1(const SHA1& _src) = default;
		SHA1(SHA1&& _src) = default;

		~SHA1() = default;

		SHA1& operator = (const SHA1&) = default;
		SHA1& operator = (SHA1&&) = default;

		std::string toString() const;

		const std::array<uint32_t, 5>& getWords() const { return m_h; }

		// A default-constructed digest stands for "not known"; no real image hashes
		// to zero.
		constexpr bool isValid() const
		{
			return m_h[0] || m_h[1] || m_h[2] || m_h[3] || m_h[4];
		}

		constexpr bool operator == (const SHA1& _sha1) const
		{
			return m_h[0] == _sha1.m_h[0] && m_h[1] == _sha1.m_h[1] && m_h[2] == _sha1.m_h[2] &&
			       m_h[3] == _sha1.m_h[3] && m_h[4] == _sha1.m_h[4];
		}

		constexpr bool operator != (const SHA1& _sha1) const
		{
			return !(*this == _sha1);
		}

		constexpr bool operator < (const SHA1& _sha1) const
		{
			for (size_t i = 0; i < 4; ++i)
			{
				if (m_h[i] < _sha1.m_h[i])	return true;
				if (m_h[i] > _sha1.m_h[i])	return false;
			}
			return m_h[4] < _sha1.m_h[4];
		}

	private:
		std::array<uint32_t, 5> m_h;
	};
}
