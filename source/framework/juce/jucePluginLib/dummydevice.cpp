#include "dummydevice.h"

namespace pluginLib
{
	DummyDevice::DummyDevice(const synthLib::DeviceCreateParams& _params) : Device(_params)
	{
	}

	void DummyDevice::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples)
	{
	}
}
