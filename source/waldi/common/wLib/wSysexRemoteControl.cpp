#include "wSysexRemoteControl.h"

#include "wMidiTypes.h"

namespace wLib
{
	SysexRemoteControl::SysexRemoteControl(const uint8_t _deviceTypeId)
		: m_deviceTypeId(_deviceTypeId)
	{
	}

	bool SysexRemoteControl::validateWaldorfSysex(const synthLib::SysexBuffer& _input) const
	{
		if(_input.size() < HeaderSize)
			return false;

		if(_input[IdxIdWaldorf] != IdWaldorf)
			return false;

		if(_input[IdxIdMachine] != m_deviceTypeId)
			return false;

		return true;
	}
}
