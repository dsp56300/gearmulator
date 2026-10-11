#pragma once

#include <vector>

#include "midiTypes.h"

namespace synthLib
{
	// Deprecated: emulator-only messages use the front panel format, see frontPanelSysex.h. Kept until the device
	// branches that still derive from it have moved
	class SysexRemoteControl
	{
	public:
		virtual ~SysexRemoteControl() = default;

		virtual bool receive(std::vector<SMidiEvent>& _output, const SysexBuffer& _input) = 0;
		virtual bool receive(const SMidiEvent& _input) = 0;
		virtual bool receive(const SysexBuffer& _input) = 0;
	};
}
