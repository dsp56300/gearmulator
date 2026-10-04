#pragma once

#include <vector>
#include <cstdint>

#include "synthLib/midiTypes.h"
#include "synthLib/sysexRemoteControl.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace wLib
{
	class SysexRemoteControl : public synthLib::SysexRemoteControl
	{
	public:
		explicit SysexRemoteControl(uint8_t _deviceTypeId);
		~SysexRemoteControl() override = default;

		// A panel frame as the device sends it: header for this device type, command, exactly _payloadSize bytes, end
		static bool isPanelFrame(const synthLib::SysexBuffer& _msg, const uint8_t _deviceTypeId, const size_t _payloadSize)
		{
			return _msg.size() == HeaderSize + _payloadSize + 1 && _msg.front() == 0xf0 && _msg[1] == IdWaldorf &&
				_msg[2] == _deviceTypeId && _msg.back() == 0xf7;
		}

	protected:
		template<typename CommandType>
		void createSysexHeader(synthLib::SysexBuffer& _dst, CommandType _cmd) const
		{
			constexpr uint8_t devId = 0;
			_dst.assign({0xf0, IdWaldorf, m_deviceTypeId, devId, static_cast<uint8_t>(_cmd)});
		}

		bool validateWaldorfSysex(const synthLib::SysexBuffer& _input) const;

		static constexpr uint8_t IdWaldorf = 0x3e;
		static constexpr size_t HeaderSize = 5;	// 0xf0, IdWaldorf, deviceTypeId, devId, command

		uint8_t m_deviceTypeId;
	};
}
