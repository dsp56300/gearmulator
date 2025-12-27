#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace rLib
{
	/* Ronaldo uses a generic address scheme of XXYYZZWW and each byte is only 7 bits. This class abstracts that storage
	 * by allocating blocks of 128 bytes
	 */
	class Storage
	{
	public:
		using Address4 = std::array<uint8_t, 4>;
		using Address3 = std::array<uint8_t, 3>;

		static constexpr Address4 InvalidAddress4{ 0xff, 0xff, 0xff, 0xff };
		static constexpr uint8_t InvalidData = 0xff;
		static constexpr uint32_t BlockShift = 7;
		static constexpr uint32_t BlockSize = 1 << BlockShift;

		using Block = std::array<uint8_t, BlockSize>;

		bool write(const Address4& _addr, const std::vector<uint8_t>& _data);
		bool write(const Address4& _addr, uint8_t _data);

		uint8_t read(const Address4& _addr) const;
		uint32_t read(std::vector<uint8_t>& _data, const Address4& _addr, uint32_t _count) const;

		static uint32_t toLinearAddress(Address4 _addr);
		static Address4 fromLinearAddress(uint32_t _addr);

		static Address3 toBlockAddress(const Address4& _addr)
		{
			return Address3{ _addr[0], _addr[1], _addr[2] };
		}

		static bool isValid(const Address4& _addr)
		{
			return (_addr[0] & 0x80) == 0 && (_addr[1] & 0x80) == 0 && (_addr[2] & 0x80) == 0 && (_addr[3] & 0x80) == 0;
		}

	private:
		std::map<Address3, Block> m_storage;
	};
}
