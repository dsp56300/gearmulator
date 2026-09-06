#pragma once

#include "xtRomWaves.h"
#include "hardwareLib/sciMidi.h"

#include "wLib/wState.h"

namespace xt
{
	class SciMidi : public hwLib::SciMidi
	{
	public:
		SciMidi(XtUc& _uc);

		void write(const synthLib::SMidiEvent& _e) override;
		void read(std::vector<uint8_t>& _result) override;

	private:
		RomWaves m_romWaves;
		wLib::Responses m_results;
	};
}
