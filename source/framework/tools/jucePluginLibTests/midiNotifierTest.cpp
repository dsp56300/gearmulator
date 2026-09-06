#include "jucePluginLibTests.h"

#include <iostream>

#include "jucePluginLib/midiNotifier.h"

#include "synthLib/midiTypes.h"

namespace
{
	using MidiNotifier = pluginLib::MidiNotifier;

	MidiNotifier::Note decode(const uint8_t _a, const uint8_t _b, const uint8_t _c, bool& _isNote)
	{
		const synthLib::SMidiEvent ev(synthLib::MidiEventSource::Host, _a, _b, _c);
		MidiNotifier::Note note;
		_isNote = MidiNotifier::toNote(ev, note);
		return note;
	}

	void testNoteDecoding()
	{
		std::cout << "Testing midi note decoding..." << std::endl;

		bool isNote = false;

		{
			const auto n = decode(0x90, 60, 100, isNote);
			TEST_ASSERT(isNote);
			TEST_ASSERT(n.on);
			TEST_ASSERT(n.note == 60);
			TEST_ASSERT(n.velocity == 100);
			TEST_ASSERT(n.channel == 1);		// channels are reported the way a musician counts them
		}

		{
			const auto n = decode(0x8f, 64, 40, isNote);
			TEST_ASSERT(isNote);
			TEST_ASSERT(!n.on);
			TEST_ASSERT(n.note == 64);
			TEST_ASSERT(n.channel == 16);
			TEST_ASSERT(n.velocity == 0);		// a note off carries no velocity to report
		}

		{
			// the one that matters: note on with velocity zero is a note off, and a keyboard that
			// misses this leaves keys stuck down
			const auto n = decode(0x92, 72, 0, isNote);
			TEST_ASSERT(isNote);
			TEST_ASSERT(!n.on);
			TEST_ASSERT(n.note == 72);
			TEST_ASSERT(n.channel == 3);
			TEST_ASSERT(n.velocity == 0);
		}

		// anything that is not a note is not reported at all
		for (const uint8_t status : {0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0xf8})
		{
			decode(status, 1, 2, isNote);
			TEST_ASSERT(!isNote);
		}

		std::cout << "  note decoding tests passed" << std::endl;
	}

	void testListenerCounting()
	{
		std::cout << "Testing midi note listeners..." << std::endl;

		MidiNotifier n;

		const auto id = n.addNoteListener([](const MidiNotifier::Note&) {});
		TEST_ASSERT(n.evNote.getListener(id).has_value());

		n.removeNoteListener(id);
		TEST_ASSERT(!n.evNote.getListener(id).has_value());

		// removing something that is not there must not unbalance the count that gates dispatch
		n.removeNoteListener(id);
		n.removeNoteListener(id);

		std::cout << "  listener tests passed" << std::endl;
	}
}

void testMidiNotifier()
{
	testNoteDecoding();
	testListenerCounting();
}
