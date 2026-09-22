#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace baseLib
{
	class SHA256
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

		// Like SHA-1, SHA-256 prints its state words big-endian, so a digest string reads
		// straight into the eight of them.
		static constexpr uint32_t parse8(const char* _digest, const size_t _offset)
		{
			return parse1(_digest[_offset    ]) << 28 | parse1(_digest[_offset + 1]) << 24 |
			       parse1(_digest[_offset + 2]) << 20 | parse1(_digest[_offset + 3]) << 16 |
			       parse1(_digest[_offset + 4]) << 12 | parse1(_digest[_offset + 5]) <<  8 |
			       parse1(_digest[_offset + 6]) <<  4 | parse1(_digest[_offset + 7]);
		}

		template<size_t N, std::enable_if_t<N == 65, void*> = nullptr> constexpr SHA256(char const(&_digest)[N])
		: m_h
		{
			parse8(_digest,  0), parse8(_digest,  8), parse8(_digest, 16), parse8(_digest, 24),
			parse8(_digest, 32), parse8(_digest, 40), parse8(_digest, 48), parse8(_digest, 56)
		}
		{
		}

		explicit SHA256(const std::vector<uint8_t>& _data);
		explicit SHA256(const uint8_t* _data, size_t _size);

		constexpr SHA256() : m_h({0,0,0,0,0,0,0,0}) {}

		SHA256(const SHA256& _src) = default;
		SHA256(SHA256&& _src) = default;

		~SHA256() = default;

		SHA256& operator = (const SHA256&) = default;
		SHA256& operator = (SHA256&&) = default;

		std::string toString() const;

		const std::array<uint32_t, 8>& getWords() const { return m_h; }

		// A default-constructed digest stands for "not known"; no real image hashes
		// to zero.
		constexpr bool isValid() const
		{
			for (const auto word : m_h)
				if (word)
					return true;
			return false;
		}

		constexpr bool operator == (const SHA256& _other) const
		{
			for (size_t i = 0; i < m_h.size(); ++i)
				if (m_h[i] != _other.m_h[i])
					return false;
			return true;
		}

		constexpr bool operator != (const SHA256& _other) const
		{
			return !(*this == _other);
		}

		constexpr bool operator < (const SHA256& _other) const
		{
			for (size_t i = 0; i < m_h.size(); ++i)
			{
				if (m_h[i] < _other.m_h[i])	return true;
				if (m_h[i] > _other.m_h[i])	return false;
			}
			return false;
		}

	private:
		std::array<uint32_t, 8> m_h;
	};
}
