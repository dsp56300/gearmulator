#include "xtMidi.h"

#include "synthLib/midiTypes.h"

namespace xt
{
	SciMidi::SciMidi(XtUc& _uc) : hwLib::SciMidi(_uc.getQSM(), 40000), m_romWaves(_uc)
	{
	}

	void SciMidi::write(const synthLib::SMidiEvent& _e)
	{
		if (m_romWaves.receiveSysEx(m_results, _e.sysex))
			return;

		hwLib::SciMidi::write(_e);
	}

	void SciMidi::read(std::vector<uint8_t>& _result)
	{
		hwLib::SciMidi::read(_result);

		for (const auto& result : m_results)
			_result.insert(_result.end(), result.begin(), result.end());
		m_results.clear();
	}
}
