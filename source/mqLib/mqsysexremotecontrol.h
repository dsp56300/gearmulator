#pragma once

#include <vector>

#include "mqmiditypes.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace mqLib
{
	class MicroQ;

	class SysexRemoteControl
	{
	public:
		SysexRemoteControl(MicroQ& _mq) : m_mq(_mq) {}

		static void createSysexHeader(std::vector<uint8_t>& _dst, SysexCommand _cmd);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const;

		bool receive(std::vector<synthLib::SMidiEvent>& _output, const std::vector<uint8_t>& _input) const;
		void handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const;

	private:
		MicroQ& m_mq;
	};
}
