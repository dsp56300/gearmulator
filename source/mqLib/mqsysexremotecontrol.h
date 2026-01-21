#pragma once

#include <vector>

#include "mqmiditypes.h"

#include "wLib/wSysexRemoteControl.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace mqLib
{
	class MicroQ;

	class SysexRemoteControl : public wLib::SysexRemoteControl
	{
	public:
		SysexRemoteControl(MicroQ& _mq);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLCDCGRam(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const;

		bool receive(std::vector<synthLib::SMidiEvent>& _output, const synthLib::SysexBuffer& _input) override;
		bool receive(const synthLib::SMidiEvent& _input) override { return false; }
		bool receive(const synthLib::SysexBuffer& _input) override { return false; }
		void handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const;

	private:
		MicroQ& m_mq;
	};
}
