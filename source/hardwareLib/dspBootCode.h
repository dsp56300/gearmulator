#pragma once

#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class DSP;
}

namespace hwLib
{
	class DspBoot
	{
	public:
		enum class State
		{
			Length,
			Address,
			Data,
			Finished
		};

		explicit DspBoot(dsp56k::DSP& _dsp);

		bool hdiWriteTX(const dsp56k::TWord& _val);

		bool finished() const { return m_state == State::Finished; }

	private:
		dsp56k::DSP& m_dsp;

		State m_state = State::Length;

		dsp56k::TWord m_remaining = 0;
		dsp56k::TWord m_address = 0;
	};
}
