#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace baseLib
{
	class MD5
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
		static constexpr uint32_t parse2(const char _b0, const char _b1)
		{
			return parse1(_b1) << 4 | parse1(_b0);
		}
		static constexpr uint32_t parse4(const char _b0, const char _b1, const char _b2, const char _b3)
		{
			return parse2(_b3, _b2) << 8 | parse2(_b1, _b0);
		}
		static constexpr uint32_t parse8(const char _b0, const char _b1, const char _b2, const char _b3, const char _b4, const char _b5, const char _b6, const char _b7)
		{
			return parse4(_b4, _b5, _b6, _b7) << 16 | parse4(_b0, _b1, _b2, _b3);
		}

		template<size_t N, std::enable_if_t<N == 33, void*> = nullptr> constexpr MD5(char const(&_digest)[N])
		: m_h
		{
			parse8(_digest[ 0], _digest[ 1], _digest[ 2], _digest[ 3], _digest[ 4], _digest[ 5], _digest[ 6], _digest[ 7]),
			parse8(_digest[ 8], _digest[ 9], _digest[10], _digest[11], _digest[12], _digest[13], _digest[14], _digest[15]),
			parse8(_digest[16], _digest[17], _digest[18], _digest[19], _digest[20], _digest[21], _digest[22], _digest[23]),
			parse8(_digest[24], _digest[25], _digest[26], _digest[27], _digest[28], _digest[29], _digest[30], _digest[31])
		}
		{
		}

		explicit MD5(const std::vector<uint8_t>& _data);
		explicit MD5(const uint8_t* _data, uint32_t _size);

		MD5() : m_h({0,0,0,0}) {}

		MD5(const MD5& _src) = default;
		MD5(MD5&& _src) = default;

		~MD5() = default;

		MD5& operator = (const MD5&) = default;
		MD5& operator = (MD5&&) = default;

		std::string toString() const;

		constexpr bool operator == (const MD5& _md5) const
		{
			return m_h[0] == _md5.m_h[0] && m_h[1] == _md5.m_h[1] && m_h[2] == _md5.m_h[2] && m_h[3] == _md5.m_h[3];
		}

		constexpr bool operator != (const MD5& _md5) const
		{
			return !(*this == _md5);
		}

		constexpr bool operator < (const MD5& _md5) const
		{
			if(m_h[0] < _md5.m_h[0])		return true;
			if(m_h[0] > _md5.m_h[0])		return false;
			if(m_h[1] < _md5.m_h[1])		return true;
			if(m_h[1] > _md5.m_h[1])		return false;
			if(m_h[2] < _md5.m_h[2])		return true;
			if(m_h[2] > _md5.m_h[2])		return false;
			if(m_h[3] < _md5.m_h[3])		return true;
			/*if(m_h[3] > _md5.m_h[3])*/	return false;
		}

	private:
		std::array<uint32_t, 4> m_h;
	};
}
