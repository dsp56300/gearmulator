#pragma once

#include "n2xtypes.h"

#include "hardwareLib/syncUCtoDSP.h"

namespace n2x
{
	class Sync : public hwLib::SyncUCtoDSP<g_ucFreqHz, g_samplerate, 16>
	{
	public:
		Sync(dsp56k::DSP& _dsp) : SyncUCtoDSP(_dsp)
		{
		}
	};
}
