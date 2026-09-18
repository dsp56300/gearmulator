#pragma once

#include <cstddef>
#include <cstdint>

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
	// The server calls this before any of the others. They pass C++ types, so it only uses a plugin built with the same
	// bridgeLib::g_protocolVersion.
	BRIDGE_CLIENT_API uint32_t bridgeProtocolVersion();

	// None of these hand over memory that the other side has to grow or free, see the bridge functions of
	// synthLib::Device. The strings of bridgeDeviceGetDesc are constants of the plugin. bridgeDeviceCreate does not throw,
	// it returns nullptr and writes why into _error, which it always terminates.
	BRIDGE_CLIENT_API synthLib::Device* bridgeDeviceCreate(const synthLib::DeviceCreateParams& _params, char* _error, size_t _errorSize);
	BRIDGE_CLIENT_API void bridgeDeviceDestroy(const synthLib::Device* _device);
	BRIDGE_CLIENT_API void bridgeDeviceGetDesc(const char*& _pluginName, const char*& _plugin4CC, uint32_t& _pluginVersion);
}
