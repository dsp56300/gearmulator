#include "hostAudioWorkgroup.h"

#include "dsp56kBase/audioworkgroup.h"

#ifdef __APPLE__
#	include <CoreAudio/CoreAudio.h>
#	include <os/workgroup.h>

#	include <vector>
#endif

namespace pluginLib
{
	HostAudioWorkgroup::HostAudioWorkgroup()
	{
#ifdef __APPLE__
		// the host starts and stops its device and the user switches devices in the host's settings, so look again
		// every now and then
		startTimer(1000);
#endif
	}

	HostAudioWorkgroup::~HostAudioWorkgroup()
	{
		stopTimer();
	}

	void HostAudioWorkgroup::timerCallback()
	{
#ifdef __APPLE__
		if(__builtin_available(macOS 11.0, *))
		{
			constexpr AudioObjectPropertyElement elementMain = 0;	// kAudioObjectPropertyElementMain, older SDKs call it ...Master

			auto get = [](const AudioObjectID _object, const AudioObjectPropertySelector _selector, auto& _value)
			{
				const AudioObjectPropertyAddress address{_selector, kAudioObjectPropertyScopeGlobal, elementMain};
				UInt32 size = sizeof(_value);
				return AudioObjectGetPropertyData(_object, &address, 0, nullptr, &size, &_value) == noErr;
			};

			const AudioObjectPropertyAddress devicesAddress{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, elementMain};
			UInt32 size = 0;
			if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size) != noErr)
				return;

			std::vector<AudioObjectID> devices(size / sizeof(AudioObjectID));
			if(AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size, devices.data()) != noErr)
				return;
			devices.resize(size / sizeof(AudioObjectID));

			for(const auto device : devices)
			{
				// kAudioDevicePropertyDeviceIsRunning is about this process, the host's device is the one that runs here
				UInt32 running = 0;
				if(!get(device, kAudioDevicePropertyDeviceIsRunning, running) || !running)
					continue;

				os_workgroup_t workgroup = nullptr;
				if(!get(device, kAudioDevicePropertyIOThreadOSWorkgroup, workgroup) || !workgroup)
					continue;

				Float64 samplerate = 0;
				UInt32 blocksize = 0;
				get(device, kAudioDevicePropertyNominalSampleRate, samplerate);
				get(device, kAudioDevicePropertyBufferFrameSize, blocksize);

				dsp56k::AudioWorkgroup::set(workgroup, static_cast<int>(samplerate), static_cast<int>(blocksize));

				// the property hands out a reference, AudioWorkgroup keeps its own
				os_release(workgroup);
				return;
			}

			dsp56k::AudioWorkgroup::set(nullptr, 0, 0);
		}
#endif
	}
}
