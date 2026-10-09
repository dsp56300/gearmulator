#pragma once

#include <thread>

#include "audioOutput.h"
#include "device.h"

namespace mqConsoleLib
{
class AudioOutputPA : public AudioOutput, public Device
{
public:
	AudioOutputPA(const ProcessCallback& _callback, const std::string& _deviceName);
	~AudioOutputPA() override;

	int portAudioCallback(void* _dst, uint32_t _frames);

	void process() override;

	int deviceIdFromName(const std::string& _devName) const override;
	std::string deviceNameFromId(int _devId) const override;

	static std::string getDeviceNameFromId(int _devId);

	bool openDevice(int _devId) override;

	int getDefaultDeviceId() const override;

private:
	void* m_stream = nullptr;
	const std::string m_deviceName;
	bool m_exit = false;
	bool m_callbackExited = false;
	std::unique_ptr<std::thread> m_thread;
};
}