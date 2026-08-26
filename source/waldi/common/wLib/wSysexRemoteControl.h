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
