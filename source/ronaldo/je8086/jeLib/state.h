#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <vector>
#include <string>
#include <map>

#include "jemiditypes.h"
#include "common/storage.h"

#include "jucePluginLib/patchdb/patchdbtypes.h"
#include "synthLib/midiTypes.h"

namespace jeLib
{
	class State
	{
	public:
		using Dump = std::vector<uint8_t>;

		using Address = uint32_t;

		static constexpr Address InvalidAddress = 0xFFFFFFFF;

		static Address getAddress(const Dump& _dump);
		static bool setAddress(Dump& _dump, Address _address);

		static AddressArea getAddressArea(const Dump& _dump);
		static AddressArea getAddressArea(Address _addr);

		static uint32_t getBankNumber(Address _addr);
		static uint32_t getProgramNumber(Address _addr);

		static std::optional<std::string> getName(const Dump& _dump);

		static bool is14BitData(Patch _param);
		static bool is14BitData(Part _param);
		static bool is14BitData(PerformanceCommon _param);

		static Dump createHeader(SysexByte _command, SysexByte _deviceId, const rLib::Storage::Address4& _address);
		static uint8_t calcChecksum(const Dump& _dump);
		static Dump& createFooter(Dump& _dump);

		static rLib::Storage::Address4 toAddress(uint32_t _addr);
		static UserPatchArea userPatchArea(uint32_t _index);
		static UserPerformanceArea userPerformanceArea(uint32_t _index);

		static rLib::Storage::Address4 toAddress(PerformanceData _performanceData, Patch _param);
		static rLib::Storage::Address4 toAddress(PerformanceData _performanceData, Part _param);
		static rLib::Storage::Address4 toAddress(PerformanceData _performanceData, PerformanceCommon _param);

		static Dump& addParameter(Dump& _dump, bool _14Bit, int _paramValue);
		
		template<typename T> static Dump& addParameter(Dump& _dump, const T _param, const int _paramValue)
		{
			return State::addParameter(_dump, is14BitData(_param), _paramValue);
		}

		static Dump createParameterChange(PerformanceData _performanceData, Patch _param, int _paramValue);
		static Dump createParameterChange(PerformanceData _performanceData, Part _param, int _paramValue);
		static Dump createParameterChange(PerformanceCommon _param, int _paramValue);
		static Dump createParameterChange(SystemParameter _param, int _paramValue);

		static Dump createPerformanceRequest(AddressArea _area, UserPerformanceArea _performanceArea);
		static Dump createSystemRequest();

		bool receive(const std::vector<synthLib::SMidiEvent>& _events);
		bool receive(const synthLib::SMidiEvent& _event);
		bool receive(const Dump& _event);

		bool createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results, PerformanceData _data, uint32_t _sizeRack, uint32_t _sizeKeyboard) const;
		bool createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results) const;
		bool createTempPerformanceDumps(std::vector<synthLib::SMidiEvent>& _results, PerformanceData _data) const;

	private:
		rLib::Storage m_tempPerformance;
		std::map<uint32_t, Dump> m_stateDumps;
	};
}
