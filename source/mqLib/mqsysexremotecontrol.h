#pragma once

#include <vector>

#include "mqmiditypes.h"

#include "synthLib/midiTypes.h"

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

		static void createSysexHeader(synthLib::SysexBuffer& _dst, SysexCommand _cmd);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLCDCGRam(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const;

		bool receive(std::vector<synthLib::SMidiEvent>& _output, const synthLib::SysexBuffer& _input) const;
		void handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const;

	private:
		MicroQ& m_mq;
	};
}
