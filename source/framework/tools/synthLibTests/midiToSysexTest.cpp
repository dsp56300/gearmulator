#include "synthLibTests.h"

#include <iostream>

#include "synthLib/midiToSysex.h"
#include "synthLib/midiTypes.h"

namespace
{
	void testSplitMultipleSysex()
	{
		std::cout << "Testing MidiToSysex::splitMultipleSysex..." << std::endl;

		using synthLib::MidiToSysex;

		// a complete message is returned as-is
		{
			const synthLib::SysexBuffer src{0xf0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x7f, 0xf7};
			synthLib::SysexBufferList msgs;
			MidiToSysex::splitMultipleSysex(msgs, src);
			TEST_ASSERT(msgs.size() == 1);
			TEST_ASSERT(msgs[0] == src);
		}

		// two messages back to back
		{
			const synthLib::SysexBuffer src{0xf0, 0x01, 0xf7, 0xf0, 0x02, 0x03, 0xf7};
			synthLib::SysexBufferList msgs;
			MidiToSysex::splitMultipleSysex(msgs, src);
			TEST_ASSERT(msgs.size() == 2);
			TEST_ASSERT(msgs[0].size() == 3);
			TEST_ASSERT(msgs[1].size() == 4);
		}

		// BUG-10313: an $f0 and an $f7 with status bytes in between are not a message. Binary files
		// of other plugins contain such a pair by coincidence, and accepting it made the preset
		// importers believe the file was parsed and stop looking for the format it really is.
		{
			const synthLib::SysexBuffer src{0xf0, 0x2d, 0x97, 0x87, 0x50, 0x00, 0xff, 0xce, 0xca, 0xf7};
			synthLib::SysexBufferList msgs;
			MidiToSysex::splitMultipleSysex(msgs, src);
			TEST_ASSERT(msgs.empty());
		}

		// a real message that follows such junk is still found
		{
			const synthLib::SysexBuffer src{0xf0, 0x97, 0x00, 0xf0, 0x11, 0x22, 0xf7};
			synthLib::SysexBufferList msgs;
			MidiToSysex::splitMultipleSysex(msgs, src);
			TEST_ASSERT(msgs.size() == 1);
			TEST_ASSERT(msgs[0] == synthLib::SysexBuffer({0xf0, 0x11, 0x22, 0xf7}));
		}

		// an unterminated message is not a message
		{
			const synthLib::SysexBuffer src{0xf0, 0x11, 0x22, 0x33};
			synthLib::SysexBufferList msgs;
			MidiToSysex::splitMultipleSysex(msgs, src);
			TEST_ASSERT(msgs.empty());
		}

		std::cout << "  splitMultipleSysex tests passed" << std::endl;
	}

	void testRemoveDuplicateFraming()
	{
		std::cout << "Testing MidiToSysex::removeDuplicateFraming..." << std::endl;

		using synthLib::MidiToSysex;
		using synthLib::SysexBuffer;

		auto stripped = [](SysexBuffer _sysex)
		{
			MidiToSysex::removeDuplicateFraming(_sysex);
			return _sysex;
		};

		const SysexBuffer message{0xf0, 0x00, 0x20, 0x33, 0x01, 0xf7};

		// a message framed once is left alone
		TEST_ASSERT(stripped(message) == message);

		// framed twice, as a wrapper that adds f0/f7 to data already carrying them delivers it
		TEST_ASSERT(stripped({0xf0, 0xf0, 0x00, 0x20, 0x33, 0x01, 0xf7, 0xf7}) == message);

		// only one end doubled
		TEST_ASSERT(stripped({0xf0, 0xf0, 0x00, 0x20, 0x33, 0x01, 0xf7}) == message);
		TEST_ASSERT(stripped({0xf0, 0x00, 0x20, 0x33, 0x01, 0xf7, 0xf7}) == message);

		std::cout << "  removeDuplicateFraming tests passed" << std::endl;
	}
}

void testMidiToSysex()
{
	testSplitMultipleSysex();
	testRemoveDuplicateFraming();
}
