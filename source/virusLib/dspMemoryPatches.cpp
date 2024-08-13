#include "dspMemoryPatches.h"

#include "dspSingle.h"

namespace virusLib
{
	static const std::initializer_list<synthLib::DspMemoryPatches> g_patches =
	{
	};

	void DspMemoryPatches::apply(const DspSingle* _dsp, const baseLib::MD5& _romChecksum)
	{
		if(!_dsp)
			return;

		for (auto element : g_patches)
			element.apply(_dsp->getDSP(), _romChecksum);
	}
}
