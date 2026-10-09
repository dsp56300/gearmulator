// ArpeggiatorTI turns Virus TI patches into synthLib::Arpeggiator settings, with the pattern table it reads from the
// firmware. A small fake firmware stands in for the real one here, which is not available to a test.
#include "arpeggiatorTI.h"

#include "dsp56kEmu/memory.h"

#include "baseLib/os.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
	using namespace virusLib;
	using namespace synthLib;

	void expect(const bool _condition, const int _line)
	{
		if (_condition)
			return;
		std::fprintf(stderr, "arpeggiatorTI_test: check in line %d failed\n", _line);
		std::abort();
	}

#define EXPECT(C) expect((C), __LINE__)

	constexpr float g_samplerate = 48000.0f;
	constexpr uint64_t g_step = 6000;	// a sixteenth at 120 BPM

	// The parts of a firmware that ArpeggiatorTI looks at: the code that picks a pattern, the table, the steps
	void writeFirmware(dsp56k::Memory& _memory)
	{
		using namespace dsp56k;

		constexpr TWord code = 0x100;
		constexpr TWord table = 0x2000;

		const TWord pick[] = {0x56f400, table, 0x0110c2, 0x44f400, table + 63, 0x200045};
		for (TWord i = 0; i < std::size(pick); ++i)
			_memory.set(MemArea_P, code + i, pick[i]);

		TWord address = 0x3000;

		auto step = [&](const double _length, const double _duration, const TWord _velocity)
		{
			_memory.set(MemArea_X, address++, static_cast<TWord>(_length * 65536.0));
			_memory.set(MemArea_X, address++, static_cast<TWord>(_duration * 65536.0));
			_memory.set(MemArea_X, address++, _velocity << 16);
		};

		for (TWord p = 1; p < 64; ++p)
		{
			_memory.set(MemArea_X, table + p, address);

			// Pattern 1: half a step at velocity 100, then a whole one at 127. Every other: a quarter step at 100
			if (p == 1)
			{
				step(0.5, 1.0, 100);
				step(1.0, 1.0, 127);
			}
			else
			{
				step(0.25, 1.0, 100);
			}

			_memory.set(MemArea_X, address++, 0xff0000);
		}
	}

	ROMFile::TPreset patch(const uint8_t _pattern)
	{
		ROMFile::TPreset p{};
		p[128 + 1] = 1;		// Arp Mode Up, where the TI keeps it
		p[128 + 2] = _pattern;
		p[128 + 5] = 64;	// Note Length, 100%
		p[128 + 16] = 57;	// Clock Tempo, 120 BPM
		p[128 + 17] = 4;	// Arp Clock, 1/16
		return p;
	}

	struct Note
	{
		uint64_t time;
		bool on;
		uint8_t velocity;

		bool operator == (const Note& _n) const { return time == _n.time && on == _n.on && velocity == _n.velocity; }
	};

	Note on(const uint64_t _time, const uint8_t _velocity) { return {_time, true, _velocity}; }
	Note off(const uint64_t _time) { return {_time, false, 64}; }

	// Holds middle C on channel 4, where the patch plays
	std::vector<Note> play(ArpeggiatorTI _arp, const ROMFile::TPreset& _patch, const uint64_t _samples)
	{
		std::array<const ROMFile::TPreset*, 16> singles{};
		singles[3] = &_patch;

		std::vector<Note> notes;
		double ppq = 0.0;

		for (uint64_t t = 0; t < _samples; t += 512)
		{
			std::vector<SMidiEvent> in, out;

			SMidiEvent transport(MidiEventSource::Internal);
			transport.type = MidiEventType::HostTransport;
			transport.transport = {ppq, 120.0f, true, true};
			in.push_back(transport);

			if (t == 0)
				in.emplace_back(MidiEventSource::Host, M_NOTEON | 3, 60, 90, 0);

			_arp.process(singles, in, out, 512, g_samplerate);

			for (const auto& e : out)
			{
				EXPECT((e.a & 0x0f) == 3 && e.b == 60);
				notes.push_back({t + e.offset, (e.a & 0xf0) == M_NOTEON, e.c});
			}

			ppq += 512 * 2.0 / g_samplerate;
		}
		return notes;
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	dsp56k::DefaultMemoryValidator validator;
	dsp56k::Memory memory(validator, 0x10000);
	writeFirmware(memory);

	ArpeggiatorTI arp;
	EXPECT(arp.readPatterns(memory));

	// The key's velocity is moved by the step's, 100 being no change. The second step is legato
	EXPECT(play(arp, patch(1), 2 * g_step + 1) == std::vector<Note>({on(0, 90), off(3000), on(6000, 117), off(12000), on(12000, 90)}));

	// Note Length scales every step, but never past the next one
	auto longer = patch(1);
	longer[128 + 5] = 127;
	EXPECT(play(arp, longer, 2 * g_step + 1) == std::vector<Note>({on(0, 90), off(5953), on(6000, 117), off(12000), on(12000, 90)}));

	// The patch's own pattern, four steps of which the first and the third play. The rest after each belongs to it:
	// the length is a share of two steps
	auto own = patch(0);
	own[256 + 127] = 3;
	const uint8_t steps[] = {64, 100, 1,  0, 0, 0,  32, 110, 1,  0, 0, 0};
	std::copy(std::begin(steps), std::end(steps), own.begin() + 384);
	EXPECT(play(arp, own, 4 * g_step + 1) == std::vector<Note>({on(0, 90), off(6000), on(12000, 100), off(15000), on(24000, 90)}));

	// Arp Clock 1/12 makes a step a third of a quarter
	auto triplets = patch(2);
	triplets[128 + 17] = 13;
	EXPECT(play(arp, triplets, 8001) == std::vector<Note>({on(0, 90), off(2000), on(8000, 90)}));

	// Without Arp Mode the note goes to the device as it is
	auto none = patch(1);
	none[128 + 1] = 0;
	EXPECT(play(arp, none, g_step).empty());

	// No pattern table, no arpeggiator: the firmware has to keep its own
	dsp56k::Memory empty(validator, 0x10000);
	ArpeggiatorTI nothing;
	EXPECT(!nothing.readPatterns(empty));
	EXPECT(!nothing.isValid());

	std::puts("arpeggiatorTI_test: all passed");
	return 0;
}
