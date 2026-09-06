#pragma once

#include <vector>

#include "xtMidiTypes.h"

#include "wLib/wSysexRemoteControl.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace xt
{
	class Xt;

	class SysexRemoteControl : public wLib::SysexRemoteControl
	{
	public:
		SysexRemoteControl(Xt& _xt);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const;

		bool receive(std::vector<synthLib::SMidiEvent>& _output, const synthLib::SysexBuffer& _input) override;
		bool receive(const synthLib::SMidiEvent& _input) override { return false; }
		bool receive(const synthLib::SysexBuffer& _input) override { return false; }
		void handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const;

	private:
		Xt& m_xt;
	};
}
