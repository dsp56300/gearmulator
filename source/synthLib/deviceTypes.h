#pragma once

namespace synthLib
{
	enum StateType
	{
		StateTypeGlobal,
		StateTypeCurrentProgram,
	};

	enum class DeviceError
	{
		Invalid = -1,
		None = 0,
		Unknown = 1,
		FirmwareMissing = 2,
	};
}
