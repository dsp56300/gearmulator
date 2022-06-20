#pragma once

#include "device.h"

class MidiDevice : public Device
{
public:
	MidiDevice(const std::string& _deviceName);
	~MidiDevice() override = default;

	static std::string getDeviceNameFromId(int _devId);

	std::string deviceNameFromId(int _devId) const override;
	int deviceIdFromName(const std::string& _devName) const override;
};
