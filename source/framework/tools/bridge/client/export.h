#pragma once

#include "bridgeLib/commands.h"

#if defined(_WIN32) && (defined(_MSC_VER) || defined(__MINGW32__))
	#define BRIDGE_CLIENT_API __declspec(dllexport)
#elif defined(_WIN32) && defined(__GNUC__)
	#define BRIDGE_CLIENT_API __attribute__((__dllexport__))
#elif defined(__GNUC__)
	#define BRIDGE_CLIENT_API __attribute__((__visibility__("default")))
#endif

namespace synthLib
{
	struct DeviceCreateParams;
	class Device;
}

extern "C"
{
	BRIDGE_CLIENT_API synthLib::Device* bridgeDeviceCreate(const synthLib::DeviceCreateParams&);
	BRIDGE_CLIENT_API void bridgeDeviceDestroy(const synthLib::Device* _device);
	BRIDGE_CLIENT_API void bridgeDeviceGetDesc(bridgeLib::PluginDesc& _desc);
}
