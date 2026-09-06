// ReSharper disable CppNonInlineFunctionDefinitionInHeaderFile
#pragma once

#include "export.h"

#include "bridgeLib/commands.h"

namespace synthLib
{
	class Device;
}

namespace bridgeClient
{
	void initPluginDesc(bridgeLib::PluginDesc& _desc)
	{
		_desc.pluginName = PluginName;
		_desc.pluginVersion = PluginVersionMajor * 10000 + PluginVersionMinor * 100 + PluginVersionPatch;
		_desc.plugin4CC = Plugin4CC;
	}

	void getBridgeDeviceDesc(bridgeLib::PluginDesc& _desc)
	{
		initPluginDesc(_desc);
	}
}

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params);

extern "C"
{
	BRIDGE_CLIENT_API synthLib::Device* bridgeDeviceCreate(const synthLib::DeviceCreateParams& _params)
	{
		return createBridgeDevice(_params);
	}

	BRIDGE_CLIENT_API void bridgeDeviceDestroy(const synthLib::Device* _device)
	{
		delete _device;
	}

	BRIDGE_CLIENT_API void bridgeDeviceGetDesc(bridgeLib::PluginDesc& _desc)
	{
		bridgeClient::getBridgeDeviceDesc(_desc);
	}
}
