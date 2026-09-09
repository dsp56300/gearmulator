#include "deviceModel.h"

#include <array>

namespace emu88Lib
{
	namespace
	{
		constexpr std::array<DeviceProfile, 5> g_profiles = {{
			{"SC-88", 2}, {"SC-88VL", 2}, {"SC-88Pro", 2}, {"SC-8850", 4}, {"SC-55mk2", 1},
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
