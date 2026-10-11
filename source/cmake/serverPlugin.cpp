// The entry point of the DSPBridge server into a product, compiled into the plugin and into its server plugin.
// createJucePlugin (juce.cmake) defines TUS_DEVICE_HEADER and TUS_DEVICE_CLASS from its last two arguments

// ReSharper disable once CppUnusedIncludeDirective
#include "client/plugin.h"

#include TUS_DEVICE_HEADER

synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams& _params)
{
	return new TUS_DEVICE_CLASS(_params);
}
