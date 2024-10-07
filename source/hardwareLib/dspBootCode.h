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

		auto getLength() const { return m_length; }
		auto getInitialPC() const { return m_initialPC; }

	private:
		dsp56k::DSP& m_dsp;

		State m_state = State::Length;

		dsp56k::TWord m_length = 0;
		dsp56k::TWord m_initialPC = 0;

		dsp56k::TWord m_remaining = 0;
		dsp56k::TWord m_address = 0;
	};
}
