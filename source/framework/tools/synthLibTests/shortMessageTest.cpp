#include "synthLibTests.h"

#include <iostream>

#include "synthLib/midiTypes.h"

// The host path used to fill b and c from past the end of a one or two byte message. Every buffer
// below carries junk behind its message, so reading one byte too many shows up as a wrong b or c.
void testShortMessage()
{
	std::cout << "Testing setShortMessage..." << std::endl;

	synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host);

	const uint8_t timingClock[] = {synthLib::M_TIMINGCLOCK, 0x55, 0x66};
	synthLib::setShortMessage(ev, timingClock, 1);
	TEST_ASSERT(ev.a == synthLib::M_TIMINGCLOCK && ev.b == 0 && ev.c == 0);

	const uint8_t programChange[] = {synthLib::M_PROGRAMCHANGE | 3, 0x05, 0x66};
	synthLib::setShortMessage(ev, programChange, 2);
	TEST_ASSERT(ev.a == (synthLib::M_PROGRAMCHANGE | 3) && ev.b == 0x05 && ev.c == 0);

	const uint8_t noteOn[] = {synthLib::M_NOTEON, 0x3c, 0x64};
	synthLib::setShortMessage(ev, noteOn, 3);
	TEST_ASSERT(ev.a == synthLib::M_NOTEON && ev.b == 0x3c && ev.c == 0x64);

	std::cout << "  setShortMessage tests passed" << std::endl;
}
