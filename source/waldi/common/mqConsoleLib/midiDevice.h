#pragma once

#include "device.h"

namespace mqConsoleLib
{
class MidiDevice : public Device
{
public:
	MidiDevice(const std::string& _deviceName, bool _output);
	~MidiDevice() override = default;

	static std::string getDeviceNameFromId(int _devId);

	std::string deviceNameFromId(int _devId) const override;
	int deviceIdFromName(const std::string& _devName) const override;

private:
	const bool m_output;
};
}