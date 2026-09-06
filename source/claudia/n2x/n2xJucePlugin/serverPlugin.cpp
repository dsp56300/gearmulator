// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include "n2xLib/n2xdevice.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new n2x::Device(_params);
}
