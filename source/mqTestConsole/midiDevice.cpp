#include "midiDevice.h"

#include "../portmidi/pm_common/portmidi.h"

MidiDevice::MidiDevice(const std::string& _deviceName): Device(_deviceName)
{
	Pm_Initialize();
}

std::string MidiDevice::getDeviceNameFromId(const int _devId)
{
	const auto* di = Pm_GetDeviceInfo(_devId);
	if(!di)
		return {};
	return di->name;
}

std::string MidiDevice::deviceNameFromId(const int _devId) const
{
	return getDeviceNameFromId(_devId);
}

int MidiDevice::deviceIdFromName(const std::string& _devName) const
{
	const auto count = Pm_CountDevices();

	for(int i=0; i<count; ++i)
	{
		const auto name = deviceNameFromId(i);
		if(name.empty())
			continue;;
		if(_devName == name)
			return i;
	}
	return -1;
}
