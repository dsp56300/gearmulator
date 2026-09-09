#include "client/plugin.h"
#include "synthLib/deviceException.h"
// Remote boot/state transport is not implemented for this experimental device.
synthLib::Device* createBridgeDevice(const synthLib::DeviceCreateParams&)
{
    throw synthLib::DeviceException(synthLib::DeviceError::Unknown,"Nord Micro Modular currently supports local emulation only.");
}
