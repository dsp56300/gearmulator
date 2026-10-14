#include "88lib/deviceModel.h"

#include <array>
#include <cstddef>

namespace emu88Lib
{
	namespace
	{
		constexpr std::array<DeviceProfile, 25> g_profiles = {{
			{"SC-88", 2}, {"SC-88VL", 2}, {"SC-88Pro", 2}, {"SC-8850", 4},
			{"SC-55mkII", 1}, {"SC-55", 1},
			{"SC-55st", 1}, {"CM-300 / SCC-1", 1}, {"SCB-55", 1}, {"RLP-3237", 1},
			{"SC-155", 1}, {"SC-155mkII", 1}, {"XPGS / G-800", 2}, {"SC-8820", 2}, {"CM-32P", 1},
			{"VE-GS Pro", 2}, {"SCC-1A", 1}, {"CM-64", 1}, {"CM-32L", 1},
			{"NU-10B (GM mode)", 1}, {"MIIG5 (GM mode)", 1},
			{"MT-32 1.x (experimental)", 1}, {"MT-32 2.x", 1}, {"CM-32LN (experimental)", 1},
			{"MV-30 (experimental)", 1},
		}};
	}

	const DeviceProfile& getDeviceProfile(const DeviceModel _model)
	{
		const auto index = static_cast<size_t>(_model);
		return index < g_profiles.size() ? g_profiles[index] : g_profiles.front();
	}

	bool isDeviceModelValue(const uint32_t _value)
	{
		return _value < g_profiles.size();
	}

	uint32_t deviceModelCount()
	{
		return static_cast<uint32_t>(g_profiles.size());
	}

	synthLib::midi::ResetTarget resetTarget(const DeviceModel _model)
	{
		return isRolandLaFamily(_model) ? synthLib::midi::ResetTarget::RolandLaModule
		                                : synthLib::midi::ResetTarget::GsModule;
	}

	std::vector<uint8_t> deviceResetSysex(const DeviceModel _model)
	{
		const auto& sysex = isRolandLaFamily(_model) ? synthLib::midi::kRolandLaResetSysex : synthLib::midi::kGsResetSysex;
		return {sysex.begin(), sysex.end()};
	}

}
