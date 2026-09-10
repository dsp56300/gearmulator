#include "synthLibTests.h"

#include "synthLib/midiTypes.h"

namespace
{
	bool isTuning(const std::initializer_list<uint8_t> _bytes)
	{
		synthLib::SysexBuffer sysex;
		sysex.assign(_bytes.begin(), _bytes.end());
		return synthLib::isUniversalTuningSysex(sysex);
	}
}

// isUniversalTuningSysex() has no caller yet, so nothing else would notice if these byte
// offsets drifted. It matches exactly two messages and has to keep ignoring everything else -
// rejecting the wrong SysEx would silently drop a dump a device needs.
void testUniversalTuning()
{
	// MIDI Tuning Bulk Dump Reply: F0 7E <device> 08 01 <program> <name...> <data...> F7
	TEST_ASSERT(isTuning({0xf0, 0x7e, 0x00, 0x08, 0x01, 0x00, 0xf7}));
	TEST_ASSERT(isTuning({0xf0, 0x7e, 0x7f, 0x08, 0x01, 0x03, 0x11, 0x22, 0xf7}));

	// Master Fine Tuning: F0 7F <device> 04 03 <lsb> <msb> F7, exactly eight bytes
	TEST_ASSERT(isTuning({0xf0, 0x7f, 0x00, 0x04, 0x03, 0x00, 0x40, 0xf7}));

	// Neighbours in the same universal families that must NOT be claimed
	TEST_ASSERT(!isTuning({0xf0, 0x7e, 0x00, 0x08, 0x02, 0x00, 0xf7}));				// single note tuning change
	TEST_ASSERT(!isTuning({0xf0, 0x7e, 0x00, 0x06, 0x01, 0xf7}));					// identity request
	TEST_ASSERT(!isTuning({0xf0, 0x7f, 0x00, 0x04, 0x01, 0x00, 0x40, 0xf7}));		// master volume
	TEST_ASSERT(!isTuning({0xf0, 0x7f, 0x00, 0x04, 0x04, 0x00, 0x40, 0xf7}));		// master coarse tuning
	TEST_ASSERT(!isTuning({0xf0, 0x7f, 0x00, 0x04, 0x03, 0x00, 0x40, 0x00, 0xf7}));	// right ids, wrong length

	// A manufacturer dump must never be mistaken for one
	TEST_ASSERT(!isTuning({0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x00, 0xf7}));		// Access Virus
	TEST_ASSERT(!isTuning({0xf0, 0x3e, 0x0e, 0x00, 0x10, 0x00, 0xf7}));				// Waldorf

	// Malformed input must be rejected, not indexed into
	TEST_ASSERT(!isTuning({}));
	TEST_ASSERT(!isTuning({0xf0, 0x7e, 0x00, 0x08, 0x01}));							// no terminator
	TEST_ASSERT(!isTuning({0x7e, 0x00, 0x08, 0x01, 0xf7}));							// no F0
	TEST_ASSERT(!isTuning({0xf0, 0x7e, 0x08, 0x01, 0xf7}));							// too short to hold the ids
}
