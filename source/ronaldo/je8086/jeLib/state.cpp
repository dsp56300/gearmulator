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
}
