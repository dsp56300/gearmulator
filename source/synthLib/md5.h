#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace synthLib
{
	class MD5
	{
	public:
		template<size_t N, std::enable_if_t<N == 33, void*> = nullptr> explicit constexpr MD5(char const(&_digest)[N])
		: m_h
		{
			static_cast<uint32_t>(_digest[0]) |
			static_cast<uint32_t>(_digest[1]) << 8 |
			static_cast<uint32_t>(_digest[2]) << 16 |
			static_cast<uint32_t>(_digest[3]) << 24,
			static_cast<uint32_t>(_digest[4]) |
			static_cast<uint32_t>(_digest[5]) << 8 |
			static_cast<uint32_t>(_digest[6]) << 16 |
			static_cast<uint32_t>(_digest[7]) << 24,
			static_cast<uint32_t>(_digest[8]) |
			static_cast<uint32_t>(_digest[9]) << 8 |
			static_cast<uint32_t>(_digest[10]) << 16 |
			static_cast<uint32_t>(_digest[11]) << 24,
			static_cast<uint32_t>(_digest[12]) |
			static_cast<uint32_t>(_digest[13]) << 8 |
			static_cast<uint32_t>(_digest[14]) << 16 |
			static_cast<uint32_t>(_digest[15]) << 24,
		}
		{
		}

		explicit MD5(const std::vector<uint8_t>& _data);

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
