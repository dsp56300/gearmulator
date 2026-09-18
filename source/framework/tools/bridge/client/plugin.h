// ReSharper disable CppNonInlineFunctionDefinitionInHeaderFile
#pragma once

#include "export.h"

#include <cstdio>
#include <exception>

#include "bridgeLib/types.h"
#include "synthLib/device.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params);

extern "C"
{
	BRIDGE_CLIENT_API uint32_t bridgeProtocolVersion()
	{
		return bridgeLib::g_protocolVersion;
	}

	BRIDGE_CLIENT_API synthLib::Device* bridgeDeviceCreate(const synthLib::DeviceCreateParams& _params, char* _error, const size_t _errorSize)
	{
		// an exception must not leave the library, its message lives on our heap
		const auto setError = [&](const char* _msg)
		{
			snprintf(_error, _errorSize, "%s", _msg);
		};

		setError("");

		try
		{
			auto* device = createBridgeDevice(_params);
			if(!device)
				setError("the plugin did not create a device");
			return device;
		}
		catch(const std::exception& e)
		{
			setError(e.what());
		}
		catch(...)
		{
			setError("unknown exception");
		}
		return nullptr;
	}

	BRIDGE_CLIENT_API void bridgeDeviceDestroy(const synthLib::Device* _device)
	{
		delete _device;
	}

	BRIDGE_CLIENT_API void bridgeDeviceGetDesc(const char*& _pluginName, const char*& _plugin4CC, uint32_t& _pluginVersion)
	{
		_pluginName = PluginName;
		_plugin4CC = Plugin4CC;
		_pluginVersion = PluginVersionMajor * 10000 + PluginVersionMinor * 100 + PluginVersionPatch;
	}
}
