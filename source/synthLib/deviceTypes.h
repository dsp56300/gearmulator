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
		Unknown,
		FirmwareMissing,
		RemoteUdpConnectFailed,
		RemoteTcpConnectFailed,
	};
}
