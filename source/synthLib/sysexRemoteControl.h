#pragma once

#include <vector>

#include "midiTypes.h"

namespace synthLib
{
	class SysexRemoteControl
	{
	public:
		virtual ~SysexRemoteControl() = default;

		virtual bool receive(std::vector<SMidiEvent>& _output, const SysexBuffer& _input) = 0;
		virtual bool receive(const SMidiEvent& _input) = 0;
		virtual bool receive(const SysexBuffer& _input) = 0;
	};
}
