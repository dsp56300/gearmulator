#pragma once

#include <vector>

#include "xtMidiTypes.h"

#include "synthLib/midiTypes.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace xt
{
	class Xt;

	class SysexRemoteControl
	{
	public:
		SysexRemoteControl(Xt& _mq) : m_mq(_mq) {}

		static void createSysexHeader(synthLib::SysexBuffer& _dst, SysexCommand _cmd);

		void sendSysexLCD(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexButtons(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexLEDs(std::vector<synthLib::SMidiEvent>& _dst) const;
		void sendSysexRotaries(std::vector<synthLib::SMidiEvent>& _dst) const;

		bool receive(std::vector<synthLib::SMidiEvent>& _output, const synthLib::SysexBuffer& _input) const;
		void handleDirtyFlags(std::vector<synthLib::SMidiEvent>& _output, uint32_t _dirtyFlags) const;

	private:
		Xt& m_mq;
	};
}
