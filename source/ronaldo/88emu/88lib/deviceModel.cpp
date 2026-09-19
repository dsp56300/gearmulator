#include "88lib/deviceModel.h"

#include <array>
#include <cstddef>

namespace emu88Lib
{
	namespace
	{
		constexpr std::array<DeviceProfile, 21> g_profiles = {{
			{"SC-88", 2}, {"SC-88VL", 2}, {"SC-88Pro", 2}, {"SC-8850", 4},
			{"SC-55mkII", 1}, {"SC-55", 1},
			{"SC-55st", 1}, {"CM-300 / SCC-1", 1}, {"SCB-55", 1}, {"RLP-3237", 1},
			{"SC-155", 1}, {"SC-155mkII", 1}, {"XPGS / G-800", 2}, {"SC-8820 (experimental)", 2}, {"CM-32P (experimental)", 1},
			{"VE-GS Pro", 2}, {"SCC-1A", 1}, {"CM-64 (experimental)", 1}, {"CM-32L (experimental)", 1},
			{"NU-10B (experimental)", 1}, {"MIIG5 (experimental)", 1},
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

}
