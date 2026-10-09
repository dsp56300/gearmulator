// MIDI events cross the DSP bridge as Command::Midi. The host's transport travels the same way, as a marker, so that the
// device on the server follows the host as a local one does
#include "commands.h"

#include "baseLib/binarystream.h"
#include "baseLib/os.h"

#include "synthLib/midiTypes.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	using namespace synthLib;

	void expect(const bool _condition, const int _line)
	{
		if (_condition)
			return;
		std::fprintf(stderr, "bridgeLib_test: check in line %d failed\n", _line);
		std::abort();
	}

#define EXPECT(C) expect((C), __LINE__)

	bool sameTransport(const HostTransport& _a, const HostTransport& _b)
	{
		return _a.ppqPos == _b.ppqPos && _a.bpm == _b.bpm && _a.isPlaying == _b.isPlaying &&	// NOLINT(clang-diagnostic-float-equal)
			_a.hasPpqPosition == _b.hasPpqPosition;
	}

	bool same(const SMidiEvent& _a, const SMidiEvent& _b)
	{
		return _a.a == _b.a && _a.b == _b.b && _a.c == _b.c && _a.sysex == _b.sysex && _a.offset == _b.offset &&
			_a.source == _b.source && _a.type == _b.type && sameTransport(_a.transport, _b.transport);
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	SMidiEvent note(MidiEventSource::Host, M_NOTEON | 3, 60, 100, 17);

	SMidiEvent dump(MidiEventSource::Editor);
	dump.sysex = {M_STARTOFSYSEX, 0x00, 0x20, 0x33, 0x01, M_ENDOFSYSEX};
	dump.offset = 4;

	SMidiEvent transport(MidiEventSource::Internal);
	transport.type = MidiEventType::HostTransport;
	transport.transport = {1234.5678901234, 97.5f, true, true};
	transport.offset = 300;

	baseLib::BinaryStream stream;
	bridgeLib::writeMidiEvent(stream, transport);
	bridgeLib::writeMidiEvent(stream, note);
	bridgeLib::writeMidiEvent(stream, dump);
	stream.setReadPos(0);

	// One event for all of them, as the receiver does it: nothing of one may stay in the next
	SMidiEvent received(MidiEventSource::Host);

	bridgeLib::readMidiEvent(stream, received);
	EXPECT(same(received, transport));

	bridgeLib::readMidiEvent(stream, received);
	EXPECT(same(received, note));
	EXPECT(received.type == MidiEventType::Midi);

	bridgeLib::readMidiEvent(stream, received);
	EXPECT(same(received, dump));

	std::puts("bridgeLib_test: all passed");
	return 0;
}
