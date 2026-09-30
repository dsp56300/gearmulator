#include "midiControllerThinner.h"

#include <cstdlib>
#include <vector>

namespace
{
	using namespace synthLib;

	std::vector<SMidiEvent> g_sent;

	void check(const bool _condition)
	{
		if(!_condition)
			std::abort();
	}

	SMidiEvent midi(const uint8_t _a, const uint8_t _b, const uint8_t _c = 0)
	{
		return SMidiEvent(MidiEventSource::Host, _a, _b, _c);
	}

	bool sent(const size_t _index, const uint8_t _a, const uint8_t _b, const uint8_t _c)
	{
		return _index < g_sent.size() && g_sent[_index].a == _a && g_sent[_index].b == _b && g_sent[_index].c == _c;
	}
}

int main()
{
	MidiControllerThinner thinner(10, [](const SMidiEvent& _ev) { g_sent.push_back(_ev); });

	// the first value goes out at once, the ones within the interval are held back and the latest one wins
	thinner.write(midi(M_CONTROLCHANGE, MC_MODULATION, 1), 100);
	thinner.write(midi(M_CONTROLCHANGE, MC_MODULATION, 2), 103);
	thinner.write(midi(M_CONTROLCHANGE, MC_MODULATION, 3), 105);
	check(g_sent.size() == 1 && sent(0, M_CONTROLCHANGE, MC_MODULATION, 1));
	thinner.flush(109);
	check(g_sent.size() == 1);
	thinner.flush(110);
	check(g_sent.size() == 2 && sent(1, M_CONTROLCHANGE, MC_MODULATION, 3));

	// a note lets the pitch bend held on its channel go first, and goes out at once itself
	thinner.write(midi(M_PITCHBEND, 0, 64), 200);
	thinner.write(midi(M_PITCHBEND, 0, 70), 202);
	thinner.write(midi(M_NOTEON, 60, 100), 203);
	check(g_sent.size() == 5 && sent(2, M_PITCHBEND, 0, 64) && sent(3, M_PITCHBEND, 0, 70) &&
		sent(4, M_NOTEON, 60, 100));

	// a note on another channel leaves the held value alone
	thinner.write(midi(M_CONTROLCHANGE | 1, MC_MODULATION, 10), 300);
	thinner.write(midi(M_CONTROLCHANGE | 1, MC_MODULATION, 11), 301);
	thinner.write(midi(M_NOTEON, 61, 100), 302);
	check(g_sent.size() == 7 && sent(6, M_NOTEON, 61, 100));
	thinner.flush(310);
	check(g_sent.size() == 8 && sent(7, M_CONTROLCHANGE | 1, MC_MODULATION, 11));

	// switches are never thinned
	for(uint32_t t = 400; t < 405; ++t)
		thinner.write(midi(M_CONTROLCHANGE, MC_SUSTAINPEDAL, (t & 1) ? 127 : 0), t);
	check(g_sent.size() == 13);

	// the sample counter may wrap
	thinner.write(midi(M_CONTROLCHANGE, 74, 1), 0xfffffffe);
	thinner.write(midi(M_CONTROLCHANGE, 74, 2), 0x2);
	thinner.flush(0x7);
	check(g_sent.size() == 14);
	thinner.flush(0x8);
	check(g_sent.size() == 15 && sent(13, M_CONTROLCHANGE, 74, 1) && sent(14, M_CONTROLCHANGE, 74, 2));

	return 0;
}
