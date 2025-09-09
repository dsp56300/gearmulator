#pragma once

namespace jeLib
{
	enum class DeviceType : uint8_t
	{
		Invalid,
		Keyboard,
		Rack
	};

	static constexpr uint32_t SinglePatchSysexSize = 0xfe;
}
