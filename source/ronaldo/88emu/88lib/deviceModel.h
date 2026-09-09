#pragma once

#include <cstdint>

namespace emu88Lib
{
	enum class DeviceModel : uint8_t
	{
		Sc88 = 0,
		Sc88VL,
		Sc88Pro,
		Sc8850,
		Sc55Mk2,
	};

	struct DeviceProfile
	{
		const char* displayName;
		uint8_t groupCount;
	};

	const DeviceProfile& getDeviceProfile(DeviceModel _model);
	bool isDeviceModelValue(uint32_t _value);
	uint32_t deviceModelCount();
}
