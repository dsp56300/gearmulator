#include "state.h"

#include <cassert>

#include "jemiditypes.h"

namespace jeLib
{
	namespace
	{
		bool match(const uint8_t _byte, SysexByte _sysexByte)
		{
			return _byte == static_cast<uint8_t>(_sysexByte);
		}

		bool validateAddressBasedDump(const State::Dump& _dump)
		{
			if (_dump.size() < std::size(g_sysexHeader))
				return false;

			if (!match(_dump[0], SysexByte::SOX))				return false;
			if (!match(_dump[1], SysexByte::ManufacturerID))	return false;
			if (!match(_dump[3], SysexByte::ModelIdMSB))		return false;
			if (!match(_dump[4], SysexByte::ModelIdLSB))		return false;

			if (!match(_dump[5], SysexByte::CommandIdDataSet1) && !match(_dump[5], SysexByte::CommandIdDataRequest1))
				return false;

			if (_dump.size() < 10)
				return false;
			return true;
		}
	}

	uint32_t State::getAddress(const Dump& _dump)
	{
		if (!validateAddressBasedDump(_dump))
			return InvalidAddress;

		const uint32_t address = 
			(static_cast<uint32_t>(_dump[6]) << 24) | 
			(static_cast<uint32_t>(_dump[7]) << 16) | 
			(static_cast<uint32_t>(_dump[8]) << 8) | 
			static_cast<uint32_t>(_dump[9]);

		return address;
	}

	bool State::setAddress(Dump& _dump, const Address _address)
	{
		if (!validateAddressBasedDump(_dump))
			return false;

		assert((_address & 0x80808080) == 0); // Only 7 bits per byte are valid
		if (_address & 0x80808080)
			return false;

		_dump[6] = static_cast<uint8_t>((_address >> 24) & 0x7f);
		_dump[7] = static_cast<uint8_t>((_address >> 16) & 0x7f);
		_dump[8] = static_cast<uint8_t>((_address >> 8) & 0x7f);
		_dump[9] = static_cast<uint8_t>(_address & 0x7f);

		return true;
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

	bool State::is14BitData(const Patch _param)
	{
		switch (_param)
		{
		case Patch::MorphBendAssign:
		case Patch::VelocitySwitch:
			return false;
		default:
			return _param >= Patch::ControlLfo1Rate && _param <= Patch::VelocityPortamentoTime;
		}
	}

	bool State::is14BitData(const PerformanceCommon _param)
	{
		switch (_param)
		{
		case PerformanceCommon::IndividualTriggerSourceNote:
		case PerformanceCommon::Tempo:
		case PerformanceCommon::VocalUpperInputJackFrontRear:
			return true;
		default:
			return false;
		}
	}

	State::Dump State::createHeader(SysexByte _command, SysexByte _deviceId, const rLib::Storage::Address4& _address)
	{
		return {
			static_cast<uint8_t>(SysexByte::SOX),
			static_cast<uint8_t>(SysexByte::ManufacturerID),
			static_cast<uint8_t>(_deviceId),
			static_cast<uint8_t>(SysexByte::ModelIdMSB),
			static_cast<uint8_t>(SysexByte::ModelIdLSB),
			static_cast<uint8_t>(_command),
			_address[0],
			_address[1],
			_address[2],
			_address[3],
			// Data follows
		};
	}

	uint8_t State::calcChecksum(const Dump& _dump)
	{
		constexpr size_t start = std::size(g_sysexHeader) + 1/*command*/;

		assert(_dump.size() > start);

		auto end = _dump.size();

		if (match(_dump.back(), SysexByte::EOX))
			end -= 2;

		assert(end > start);

		uint8_t checksum = 0;
		for (size_t i = start; i < end; ++i)
			checksum += _dump[i];
		checksum &= 0x7f;
		checksum = (128 - checksum) & 0x7f;

		return checksum;
	}

	State::Dump& State::createFooter(Dump& _dump)
	{
		assert(!_dump.empty());
		assert(_dump.back() != 0xf7);

		_dump.push_back(calcChecksum(_dump));
		_dump.push_back(static_cast<uint8_t>(SysexByte::EOX));

		return _dump;
	}

	rLib::Storage::Address4 State::toAddress(const uint32_t _addr)
	{
		assert((_addr & 0x80808080) == 0); // Only 7 bits per byte are valid

		return {
			static_cast<uint8_t>((_addr >> 24) & 0x7f),
			static_cast<uint8_t>((_addr >> 16) & 0x7f),
			static_cast<uint8_t>((_addr >> 8) & 0x7f),
			static_cast<uint8_t>(_addr & 0x7f),
		};
	}

	UserPatchArea State::userPatchArea(uint32_t _index)
	{
		assert(_index < 128);

		constexpr auto step = static_cast<uint32_t>(UserPatchArea::UserPatch002) - static_cast<uint32_t>(UserPatchArea::UserPatch001);

		if (_index < 64)
		{
			return static_cast<UserPatchArea>(_index * step + static_cast<uint32_t>(UserPatchArea::UserPatch001));
		}

		_index -= 64;

		return static_cast<UserPatchArea>(_index * step + static_cast<uint32_t>(UserPatchArea::UserPatch065));
	}

	UserPerformanceArea State::userPerformanceArea(const uint32_t _index)
	{
		assert(_index < 64);
		constexpr auto step = static_cast<uint32_t>(UserPerformanceArea::UserPerformance02) - static_cast<uint32_t>(UserPerformanceArea::UserPerformance01);
		return static_cast<UserPerformanceArea>(_index * step + static_cast<uint32_t>(UserPerformanceArea::UserPerformance01));
	}

	rLib::Storage::Address4 State::toAddress(const PerformanceData _performanceData, Patch _param)
	{
		uint32_t addr = static_cast<uint32_t>(AddressArea::PerformanceTemp);
		addr |= static_cast<uint32_t>(_performanceData);
		addr |= static_cast<uint32_t>(_param);
		return toAddress(addr);
	}

	State::Dump& State::addParameter(Dump& _dump, const Patch _param, int _paramValue)
	{
		if (is14BitData(_param))
		{
			_dump.push_back(static_cast<uint8_t>((_paramValue >> 7) & 0x7F));  // MSB
			_dump.push_back(static_cast<uint8_t>(_paramValue & 0x7F));         // LSB
		}
		else
		{
			_dump.push_back(static_cast<uint8_t>(_paramValue & 0x7F));
		}

		return _dump;
	}

	State::Dump State::createParameterChange(const PerformanceData _performanceData, const Patch _param, const int _paramValue)
	{
		assert(
			_performanceData == PerformanceData::PatchUpper ||
			_performanceData == PerformanceData::PatchLower
		);

		const auto addr = toAddress(_performanceData, _param);
		Dump dump = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr);

		addParameter(dump, _param, _paramValue);

		return createFooter(dump);
	}

	State::Dump State::createPerformanceRequest(AddressArea _area, UserPerformanceArea _performanceArea)
	{
		assert(_area == AddressArea::PerformanceTemp || _area == AddressArea::UserPerformance);

		auto addr = static_cast<uint32_t>(_area);

		if (_area != AddressArea::PerformanceTemp)
			addr += static_cast<uint32_t>(_performanceArea);

		Dump dump = createHeader(SysexByte::CommandIdDataRequest1, SysexByte::DeviceIdDefault, toAddress(addr));
		auto addr4 = toAddress(static_cast<uint32_t>(UserPerformanceArea::BlockSize));
		dump.insert(dump.end(), addr4.begin(), addr4.end());
		return createFooter(dump);
	}
}
