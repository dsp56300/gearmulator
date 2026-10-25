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

	void testPitchBendAndPressure()
	{
		std::cout << "Testing pitch bend and channel pressure..." << std::endl;

		MidiNotifier n;

		auto send = [&n](const uint8_t _a, const uint8_t _b, const uint8_t _c, const synthLib::MidiEventSource _source = synthLib::MidiEventSource::Host)
		{
			n.onMidiEvent(synthLib::SMidiEvent(_source, _a, _b, _c));
		};

		TEST_ASSERT(n.getPitchBend(1) == 0);
		TEST_ASSERT(n.getChannelPressure(16) == 0);

		send(0xe0, 0x00, 0x40);			// centre
		TEST_ASSERT(n.getPitchBend(1) == 0);
		send(0xe0, 0x7f, 0x7f);			// LSB first, MSB second
		TEST_ASSERT(n.getPitchBend(1) == 8191);
		send(0xe0, 0x00, 0x00);
		TEST_ASSERT(n.getPitchBend(1) == -8192);
		send(0xef, 0x01, 0x40);
		TEST_ASSERT(n.getPitchBend(16) == 1);
		TEST_ASSERT(n.getPitchBend(1) == -8192);	// channels are kept apart

		send(0xd2, 99, 0);
		TEST_ASSERT(n.getChannelPressure(3) == 99);
		TEST_ASSERT(n.getChannelPressure(4) == 0);

		// poly pressure is per note, not the channel pressure
		send(0xa2, 60, 10);
		TEST_ASSERT(n.getChannelPressure(3) == 99);

		// Reset All Controllers resets both, on its own channel only
		send(0xb2, 121, 0);
		TEST_ASSERT(n.getChannelPressure(3) == 0);
		send(0xb0, 121, 0);
		TEST_ASSERT(n.getPitchBend(1) == 0);
		TEST_ASSERT(n.getPitchBend(16) == 1);

		// what the device sends out does not count, only what it receives
		send(0xe0, 0x00, 0x00, synthLib::MidiEventSource::Device);
		send(0xd0, 50, 0, synthLib::MidiEventSource::Device);
		TEST_ASSERT(n.getPitchBend(1) == 0);
		TEST_ASSERT(n.getChannelPressure(1) == 0);
		send(0xe0, 0x00, 0x00, synthLib::MidiEventSource::Physical);
		TEST_ASSERT(n.getPitchBend(1) == -8192);

		std::cout << "  pitch bend and channel pressure tests passed" << std::endl;
	}
}

void testMidiNotifier()
{
	testNoteDecoding();
	testListenerCounting();
	testPitchBendAndPressure();
}
