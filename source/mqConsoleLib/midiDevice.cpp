#include "midiDevice.h"

#include "../portmidi/pm_common/portmidi.h"

#ifdef ANDROID
extern "C"
{
	// nop implementation of PortMidi

	PmError Pm_Initialize( void )
	{
		return pmHostError;
	}
	const PmDeviceInfo* Pm_GetDeviceInfo( PmDeviceID id)
	{
		return nullptr;
	}
	int Pm_CountDevices( void )
	{
		return 0;
	}
	PmDeviceID Pm_GetDefaultInputDeviceID( void )
	{
		return 0;
	}
	PmDeviceID Pm_GetDefaultOutputDeviceID( void )
	{
		return 0;
	}
	PmError Pm_Close( PortMidiStream* stream )
	{
		return pmNoError;
	}
	PmError Pm_WriteSysEx(PortMidiStream *stream, PmTimestamp when, unsigned char *msg)
	{
		return pmHostError;
	}
	const char *Pm_GetErrorText( PmError errnum )
	{
		return "Platform not supported";
	}
	PmError Pm_Write( PortMidiStream *stream, PmEvent *buffer, int32_t length)
	{
		return pmHostError;
	}
	PmError Pm_OpenOutput(PortMidiStream** stream, PmDeviceID outputDevice, void *outputDriverInfo,int32_t bufferSize, PmTimeProcPtr time_proc, void *time_info, int32_t latency )
	{
		return pmHostError;
	}
	PmError Pm_Poll( PortMidiStream *stream )
	{
		return pmNoData;
	}
	int Pm_Read(PortMidiStream *stream, PmEvent *buffer, int32_t length)
	{
		return 0;
	}
	PmError Pm_OpenInput(PortMidiStream** stream, PmDeviceID inputDevice, void *inputDriverInfo, int32_t bufferSize, PmTimeProcPtr time_proc, void *time_info)
	{
		return pmHostError;
	}
}
#endif

MidiDevice::MidiDevice(const std::string& _deviceName, bool _output): Device(_deviceName), m_output(_output)
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
		const auto* di = Pm_GetDeviceInfo(i);

		if(m_output && !di->output)
			continue;
		if(!m_output && !di->input)
			continue;

		const auto name = deviceNameFromId(i);
		if(name.empty())
			continue;;
		if(_devName == name)
			return i;
	}
	return -1;
}
