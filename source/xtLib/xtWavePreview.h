#pragma once

namespace dsp56k
{
	class Memory;
}

namespace xt
{
	class Xt;

	class WavePreview
	{
	public:
		WavePreview(Xt& _xt);

	private:
		Xt& m_xt;
		dsp56k::Memory& m_dspMem;
	};
}
