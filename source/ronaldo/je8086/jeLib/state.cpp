#include "state.h"

#include <cassert>

#include "jemiditypes.h"

namespace jeLib
{
	namespace
	{
		constexpr size_t g_nameStart = 10;
		constexpr size_t g_nameSize = 16;

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

	bool State::setBankNumber(Dump& _dump, uint8_t _bank)
	{
		const auto area = getAddressArea(_dump);

		if (area != AddressArea::UserPatch)
			return false;

		auto addr = getAddress(_dump);

		addr &= ~static_cast<uint32_t>(0x00ff0000);
		addr |= (static_cast<uint32_t>(_bank) << 16) & 0x00ff0000;

		return setAddress(_dump, addr);
	}

	bool State::setProgramNumber(Dump& _dump, uint8_t _bank)
	{
		const auto area = getAddressArea(_dump);

		auto addr = getAddress(_dump);

		switch (area)
		{
			case AddressArea::UserPerformance:
				addr &= ~static_cast<uint32_t>(0x00ff0000);
				addr |= (static_cast<uint32_t>(_bank) << 16) & 0x00ff0000;
				return setAddress(_dump, addr);
			case AddressArea::UserPatch:
				addr &= ~static_cast<uint32_t>(0xff00);
				addr |= (static_cast<uint32_t>(_bank) << 9) & 0xff00;
				return setAddress(_dump, addr);
			default:
				return false;
		}
	}

	std::optional<std::string> State::getName(const Dump& _dump)
	{
		const auto area = getAddressArea(_dump);

		if (area == AddressArea::PerformanceTemp)
		{
			auto performanceArea = static_cast<PerformanceData>(getAddress(_dump) & static_cast<uint32_t>(PerformanceData::BlockMask));
			switch (performanceArea)
			{
			case PerformanceData::PerformanceCommon:
			case PerformanceData::PatchUpper:
			case PerformanceData::PatchLower:
				// these are fine
				break;
			default:
				// all others are not
				return {};
			}
		}
		else if (area != AddressArea::UserPerformance && area != AddressArea::UserPatch)
			return {};

		if (_dump.size() < g_nameStart + g_nameSize)
			return {};

		std::string name;
		name.reserve(g_nameSize);

		for (size_t i = 0; i < g_nameSize; ++i)
		{
			const auto c = static_cast<char>(_dump[g_nameStart + i]);
			name.push_back(c);
		}

		return name;
	}

	bool State::setName(Dump& _dump, const std::string& _name)
	{
		const auto area = getAddressArea(_dump);

		if (area != AddressArea::UserPerformance && area != AddressArea::UserPatch)
			return false;

		if (_dump.size() < g_nameStart + g_nameSize)
			return false;
		for (size_t i = 0; i < g_nameSize; ++i)
		{
			char c = (i < _name.size()) ? _name[i] : ' ';
			if (c < 32 || c > 126)
				c = ' ';
			_dump[g_nameStart + i] = static_cast<uint8_t>(c);
		}
		return true;
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

	bool State::is14BitData(Part)
	{
		return false;
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

	bool State::updateChecksum(Dump& _dump)
	{
		if (_dump.empty() || _dump.back() != 0xf7)
			return false;
		_dump[_dump.size() - 2] = calcChecksum(_dump);
		return true;
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

	rLib::Storage::Address4 State::toAddress(PerformanceData _performanceData, Part _param)
	{
		uint32_t addr = static_cast<uint32_t>(AddressArea::PerformanceTemp);
		addr |= static_cast<uint32_t>(_performanceData);
		addr |= static_cast<uint32_t>(_param);
		return toAddress(addr);
	}

	rLib::Storage::Address4 State::toAddress(PerformanceData _performanceData, PerformanceCommon _param)
	{
		uint32_t addr = static_cast<uint32_t>(AddressArea::PerformanceTemp);
		addr |= static_cast<uint32_t>(_performanceData);
		addr |= static_cast<uint32_t>(_param);
		return toAddress(addr);
	}

	State::Dump& State::addParameter(Dump& _dump, const bool _14Bit, const int _paramValue)
	{
		if (_14Bit)
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

	State::Dump State::createParameterChange(PerformanceData _performanceData, const Part _param, const int _paramValue)
	{
		assert(
			_performanceData == PerformanceData::PartUpper ||
			_performanceData == PerformanceData::PartLower
		);

		const auto addr = toAddress(_performanceData, _param);
		Dump dump = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr);

		addParameter(dump, _param, _paramValue);

		return createFooter(dump);
	}

	State::Dump State::createParameterChange(const PerformanceCommon _param, const int _paramValue)
	{
		const auto addr = toAddress(PerformanceData::PerformanceCommon, _param);
		Dump dump = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr);

		addParameter(dump, _param, _paramValue);

		return createFooter(dump);
	}

	State::Dump State::createParameterChange(const SystemParameter _param, const int _paramValue)
	{
		const auto addr = toAddress(static_cast<uint32_t>(AddressArea::System) | static_cast<uint32_t>(SystemArea::SystemParameter) | static_cast<uint32_t>(_param));
		Dump dump = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr);
		addParameter(dump, false, _paramValue);
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

	State::Dump State::createSystemRequest()
	{
		Dump dump = createHeader(SysexByte::CommandIdDataRequest1, SysexByte::DeviceIdDefault, toAddress(static_cast<uint32_t>(SystemArea::SystemParameter)));
		auto addr4 = toAddress(0x4000);
		dump.insert(dump.end(), addr4.begin(), addr4.end());
		createFooter(dump);
		return dump;
	}

	bool State::receive(const std::vector<synthLib::SMidiEvent>& _events)
	{
		bool result = false;

		for (const auto& event : _events)
		{
			if (receive(event))
				result = true;
		}
		return result;
	}

	bool State::receive(const synthLib::SMidiEvent& _event)
	{
		return receive(_event.sysex);
	}

	bool State::receive(const Dump& _sysex)
	{
		if (_sysex.empty())
			return false;

		auto addr = getAddress(_sysex);
		auto area = getAddressArea(addr);

		if (area != AddressArea::PerformanceTemp)
			return false;

		const auto addr4 = toAddress(addr);

		constexpr auto firstDataOffset = std::size(g_sysexHeader) + 1 + std::tuple_size_v<decltype(addr4)>;

		std::vector<uint8_t> dataToWrite;

		dataToWrite.insert(dataToWrite.end(), _sysex.begin() + firstDataOffset, _sysex.end() - 2 /*checksum + eox*/);

		m_tempPerformance.write(addr4, dataToWrite);

		return true;
	}

	bool State::createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results, const PerformanceData _data, const uint32_t _sizeRack, const uint32_t _sizeKeyboard) const
	{
		auto addr = static_cast<uint32_t>(AddressArea::PerformanceTemp) | static_cast<uint32_t>(_data);
		auto addr4 = toAddress(addr);

		synthLib::SMidiEvent event(synthLib::MidiEventSource::Device);

		event.sysex = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr4);

		constexpr auto sizeLimit = static_cast<uint32_t>(Patch::DataLengthLimitPerDump);

		auto size = std::min(sizeLimit, std::max(_sizeRack, _sizeKeyboard));

		auto numRead = m_tempPerformance.read(event.sysex, addr4, size);
		assert(numRead == _sizeRack || numRead == _sizeKeyboard || numRead == sizeLimit);

		if (!numRead)
			return false;

		createFooter(event.sysex);

		_results.push_back(event);

		if (numRead == sizeLimit && (_data == PerformanceData::PatchLower || _data == PerformanceData::PatchUpper))
		{
			// single dump size is limited to sizeLimit bytes. Request the second dump if needed

			addr4[2] += sizeLimit >> 7;
			addr4[3] += sizeLimit & 0x7f;

			event.sysex = createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr4);

			size = std::max(_sizeRack, _sizeKeyboard) - sizeLimit;

			numRead = m_tempPerformance.read(event.sysex, addr4, std::max(_sizeRack, _sizeKeyboard));
			assert(numRead == size);

			if (!numRead)
				return true;

			createFooter(event.sysex);

			_results.push_back(event);
		}

		return true;
	}

	bool State::createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results) const
	{
		if (rLib::Storage::InvalidData == m_tempPerformance.read(toAddress(static_cast<uint32_t>(AddressArea::PerformanceTemp))))
			return false;

		bool result = false;

		result |= createTempPerformanceDumps(_results, PerformanceData::PerformanceCommon);
		result |= createTempPerformanceDumps(_results, PerformanceData::VoiceModulator   );
		result |= createTempPerformanceDumps(_results, PerformanceData::PartUpper        );
		result |= createTempPerformanceDumps(_results, PerformanceData::PartLower        );
		result |= createTempPerformanceDumps(_results, PerformanceData::PatchUpper       );
		result |= createTempPerformanceDumps(_results, PerformanceData::PatchLower       );

		return result;
	}

	bool State::createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results, const PerformanceData _data) const
	{
		if (rLib::Storage::InvalidData == m_tempPerformance.read(toAddress(static_cast<uint32_t>(AddressArea::PerformanceTemp))))
			return false;

		switch (_data)
		{
		case PerformanceData::PerformanceCommon:
			return createTempPerformanceDumps(_results, _data, static_cast<uint32_t>(PerformanceCommon::DataLengthRack), static_cast<uint32_t>(PerformanceCommon::DataLengthKeyboard));
		case PerformanceData::VoiceModulator:
			return createTempPerformanceDumps(_results, _data, static_cast<uint32_t>(VoiceModulator::DataLengthRack), static_cast<uint32_t>(VoiceModulator::DataLengthKeyboard));
		case PerformanceData::PartUpper:
		case PerformanceData::PartLower:
			return createTempPerformanceDumps(_results, _data, static_cast<uint32_t>(Part::DataLengthRack), static_cast<uint32_t>(Part::DataLengthKeyboard));
		case PerformanceData::PatchUpper:
		case PerformanceData::PatchLower:
			return createTempPerformanceDumps(_results, _data, static_cast<uint32_t>(Patch::DataLengthRack), static_cast<uint32_t>(Patch::DataLengthKeyboard));
		default:
			return false;
		}
	}
}
