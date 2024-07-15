#pragma once

#include "synthLib/dspMemoryPatch.h"

namespace virusLib
{
	class DspSingle;

	class DspMemoryPatches
	{
	public:
		static void apply(const DspSingle* _dsp, const synthLib::MD5& _romChecksum);
	};
}
