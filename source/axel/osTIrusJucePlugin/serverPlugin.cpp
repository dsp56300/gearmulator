// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include "virusLib/device.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new virusLib::Device(_params);
}
