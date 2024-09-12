#include "xtWavePreview.h"

#include "xt.h"
#include "xtHardware.h"

namespace xt
{
	WavePreview::WavePreview(Xt& _xt)
		: m_xt(_xt)
		, m_dspMem(m_xt.getHardware()->getDSP(0).dsp().memory())
	{
	}
}
