// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"
#include "jeLib/device.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new jeLib::Device(_params);
}
