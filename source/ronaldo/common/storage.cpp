#include "storage.h"

#include <cassert>

namespace rLib
{
	bool Storage::write(const Address4& _addr, const std::vector<uint8_t>& _data)
	{
		if (_data.empty())
			return false;

		const auto linearAddr = toLinearAddress(_addr);

		for (size_t i=0; i<_data.size(); ++i)
		{
			if (!write(fromLinearAddress(linearAddr + static_cast<uint32_t>(i)), _data[i]))
				return false;
		}

		return true;
	}

	bool Storage::write(const Address4& _addr, const uint8_t _data)
	{
		assert(_data < 0x80); // Only 7 bits are valid

		const auto block = toBlockAddress(_addr);
		const auto offset = _addr.back();

		auto it = m_storage.find(block);
		if (it == m_storage.end())
		{
			it = m_storage.emplace(block, Block{}).first;
			it->second.fill(InvalidData);
		}
		it->second[offset] = _data;
		return true;
	}

	uint8_t Storage::read(const Address4& _addr) const
	{
		const auto it = m_storage.find(toBlockAddress(_addr));
		if (it == m_storage.end())
			return InvalidData;
		return it->second[_addr.back()];
	}

	uint32_t Storage::read(std::vector<uint8_t>& _data, const Address4& _addr, const uint32_t _count) const
	{
		_data.reserve(_data.size() + _count);
		const auto linearAddr = toLinearAddress(_addr);
		for (uint32_t i = 0; i < _count; ++i)
		{
			auto b = read(fromLinearAddress(linearAddr + i));
			if (b == InvalidData)
				return i;
			_data.push_back(b);
		}
		return _count;
	}

	uint32_t Storage::toLinearAddress(const Address4 _addr)
	{
		return
			(static_cast<uint32_t>(_addr[0]) << 21) |
			(static_cast<uint32_t>(_addr[1]) << 14) |
			(static_cast<uint32_t>(_addr[2]) << 7) |
			static_cast<uint32_t>(_addr[3]);
	}

	Storage::Address4 Storage::fromLinearAddress(const uint32_t _addr)
	{
		return Address4{
			static_cast<uint8_t>((_addr >> 21) & 0x7f),
			static_cast<uint8_t>((_addr >> 14) & 0x7f),
			static_cast<uint8_t>((_addr >> 7) & 0x7f),
			static_cast<uint8_t>(_addr & 0x7f)
		};
	}
}
