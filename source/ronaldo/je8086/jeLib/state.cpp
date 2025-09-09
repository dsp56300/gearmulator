#include "state.h"

#include "jemiditypes.h"

namespace jeLib
{
	namespace
	{
		bool match(const uint8_t _byte, SysexByte _sysexByte)
		{
			return _byte == static_cast<uint8_t>(_sysexByte);
		}
	}

	uint32_t State::getAddress(const Dump& _dump)
	{
		if (_dump.size() < std::size(g_sysexHeader))
			return InvalidAddress;

		if (!match(_dump[0], SysexByte::SOX))				return InvalidAddress;
		if (!match(_dump[1], SysexByte::ManufacturerID))	return InvalidAddress;
		if (!match(_dump[3], SysexByte::ModelIdMSB))		return InvalidAddress;
		if (!match(_dump[4], SysexByte::ModelIdLSB))		return InvalidAddress;

		if (!match(_dump[5], SysexByte::CommandIdDataSet1) && !match(_dump[5], SysexByte::CommandIdDataRequest1))
			return InvalidAddress;

		if (_dump.size() < 10)
			return InvalidAddress;

		const uint32_t address = 
			(static_cast<uint32_t>(_dump[6]) << 24) | 
			(static_cast<uint32_t>(_dump[7]) << 16) | 
			(static_cast<uint32_t>(_dump[8]) << 8) | 
			static_cast<uint32_t>(_dump[9]);

		return address;
	}

	AddressArea State::getAddressArea(const Dump& _dump)
	{
		return getAddressArea(getAddress(_dump));
	}

	AddressArea State::getAddressArea(Address _addr)
	{
		if (_addr == InvalidAddress)
			return AddressArea::Invalid;

		_addr &= static_cast<uint32_t>(AddressArea::Mask);

		return static_cast<AddressArea>(_addr);
	}

	uint32_t State::getBankNumber(Address _addr)
	{
		const auto area = getAddressArea(_addr);

		switch (area)
		{
			case AddressArea::UserPerformance:
				return 0;
			case AddressArea::UserPatch:
				return (_addr & 0x00ffffff) >> 16;
			default:
				return 0;
		}
	}

	uint32_t State::getProgramNumber(Address _addr)
	{
		const auto area = getAddressArea(_addr);
		switch (area)
		{
			case AddressArea::UserPerformance:
				return (_addr & 0x00ff0000) >> 16;
			case AddressArea::UserPatch:
				return (_addr & 0xff00) >> 9;
			default:
				return 0;
		}
	}

	std::optional<std::string> State::getName(const Dump& _dump)
	{
		const auto area = getAddressArea(_dump);

		constexpr size_t start = 10;
		constexpr size_t size = 16;

		if (area != AddressArea::UserPerformance && area != AddressArea::UserPatch)
			return {};

		if (_dump.size() < start + size)
			return {};

		std::string name;
		name.reserve(size);

		for (size_t i = 0; i < size; ++i)
		{
			const auto c = static_cast<char>(_dump[start + i]);
			name.push_back(c);
		}

		return name;
	}
}
