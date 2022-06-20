#include "device.h"

std::string Device::getDeviceName() const
{
	if(m_deviceName.empty())
		return deviceNameFromId(m_deviceId);
	return m_deviceName;
}

bool Device::openDevice()
{
	if(!getDeviceName().empty())
	{
		const auto devId = deviceIdFromName(getDeviceName());

		if(devId >= 0)
		{
			if(openDevice(devId))
			{
				m_deviceId = devId;
				return true;
			}

			return openDefaultDevice();
		}
	}

	return openDefaultDevice();
}

bool Device::openDefaultDevice()
{
	if(!openDevice(getDefaultDeviceId()))
		return false;
	m_deviceId = getDefaultDeviceId();
	return true;
}
