#pragma once

#include <string>

class Device
{
public:
	explicit Device(std::string _deviceName) : m_deviceName(std::move(_deviceName)) {}
	virtual ~Device() = default;
	
	virtual std::string getDeviceName() const;
	virtual int getDeviceId() const { return m_deviceId; }
	virtual int getDefaultDeviceId() const = 0;

	virtual std::string deviceNameFromId(int _devId) const = 0;
	virtual int deviceIdFromName(const std::string& _devName) const = 0;

	bool openDevice();
	virtual bool openDevice(int devId) = 0;

private:
	bool openDefaultDevice();

	std::string m_deviceName;

protected:
	int m_deviceId = -1;
};
