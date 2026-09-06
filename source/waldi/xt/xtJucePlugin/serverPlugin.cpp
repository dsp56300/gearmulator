// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include "xtLib/xtDevice.h"

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new xt::Device(_params);
}
