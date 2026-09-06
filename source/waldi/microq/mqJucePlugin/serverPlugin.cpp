// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include "mqLib/device.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new mqLib::Device(_params);
}
