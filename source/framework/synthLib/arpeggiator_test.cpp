// The arpeggiator replaces firmware arpeggiators that follow MIDI clock and drift from the host. Everything here checks
// the sample a note lands on: on the grid, across blocks of any size, across loop wraps, and with swing and patterns.
#include "arpeggiator.h"
#include "device.h"
#include "plugin.h"

#include "baseLib/os.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
	using namespace synthLib;

	void expect(const bool _condition, const int _line)
	{
		if (_condition)
			return;
		std::fprintf(stderr, "arpeggiator_test: check in line %d failed\n", _line);
		std::abort();
	}

#define EXPECT(C) expect((C), __LINE__)

	constexpr float g_samplerate = 48000.0f;
	constexpr uint64_t g_sixteenth = 6000;	// samples, at 120 BPM and 48 kHz

	struct Event
	{
		uint64_t time;
		uint8_t a, b, c;

		bool operator == (const Event& _e) const { return time == _e.time && a == _e.a && b == _e.b && c == _e.c; }
	};

	Event on(const uint64_t _time, const uint8_t _note, const uint8_t _velocity = 100)
	{
		return {_time, M_NOTEON, _note, _velocity};
	}

	Event off(const uint64_t _time, const uint8_t _note, const uint8_t _velocity = 64)
	{
		return {_time, M_NOTEOFF, _note, _velocity};
	}

	ArpSettings settings(const ArpMode _mode)
	{
		ArpSettings s;
		s.mode = _mode;
		return s;
	}

	SMidiEvent transportEvent(const double _ppq, const float _bpm, const bool _playing, const uint32_t _offset)
	{
		SMidiEvent e(MidiEventSource::Internal);
		e.type = MidiEventType::HostTransport;
		e.transport = {_ppq, _bpm, _playing, true};
		e.offset = _offset;
		return e;
	}

	// Plays the keys in 'input' through the arpeggiator, the way a device gets them from the plugin: block by block, with
	// the host's transport at the block start
	struct Host
	{
		Arpeggiator arp;
		float bpm = 120.0f;
		bool playing = true;
		double ppq = 0.0;			// at the start of the next block
		double loopEnd = 0.0;		// loops back to 0 when the position reaches it, at a block start
		bool loopCarries = true;	// false: the host wraps late, and the overshoot is lost
		int transportOffset = 0;	// where in the block the transport comes, -1 for no transport at all
		uint64_t time = 0;
		std::vector<Event> input;
		std::vector<Event> played;	// what the arpeggiator plays
		std::vector<Event> passed;	// what it leaves for the device

		explicit Host(const ArpSettings& _settings)
		{
			arp.setSettings(_settings);
		}

		void run(const uint64_t _until, const uint32_t _blockSize)
		{
			std::vector<SMidiEvent> midiIn;
			std::vector<SMidiEvent> out;

			while (time < _until)
			{
				const auto count = static_cast<uint32_t>(std::min<uint64_t>(_blockSize, _until - time));

				if (loopEnd > 0.0 && ppq >= loopEnd)
					ppq = loopCarries ? ppq - loopEnd : 0.0;

				midiIn.clear();
				out.clear();

				for (const auto& e : input)
				{
					if (e.time >= time && e.time < time + count)
						midiIn.emplace_back(MidiEventSource::Host, e.a, e.b, e.c, static_cast<uint32_t>(e.time - time));
				}

				if (transportOffset >= 0 && static_cast<uint32_t>(transportOffset) < count)
				{
					const auto at = static_cast<uint32_t>(transportOffset);
					const auto pos = ppq + (playing ? at * static_cast<double>(bpm) / (60.0 * g_samplerate) : 0.0);
					auto it = midiIn.begin();
					while (it != midiIn.end() && it->offset < at)
						++it;
					midiIn.insert(it, transportEvent(pos, bpm, playing, at));
				}

				arp.process(midiIn, out, count, g_samplerate);

				for (const auto& e : out)
					played.push_back({time + e.offset, e.a, e.b, e.c});
				for (const auto& e : midiIn)
				{
					// the transport stays for the device too, but it is no MIDI
					if (e.type == MidiEventType::Midi)
						passed.push_back({time + e.offset, e.a, e.b, e.c});
				}

				if (playing)
					ppq += count * static_cast<double>(bpm) / (60.0 * g_samplerate);
				time += count;
			}
		}

		std::vector<Event> noteOns() const
		{
			std::vector<Event> ons;
			for (const auto& e : played)
			{
				if ((e.a & 0xf0) == M_NOTEON)
					ons.push_back(e);
			}
			return ons;
		}

		std::vector<uint64_t> noteOnTimes() const
		{
			std::vector<uint64_t> times;
			for (const auto& e : noteOns())
				times.push_back(e.time);
			return times;
		}

		std::vector<uint8_t> notes() const
		{
			std::vector<uint8_t> notes;
			for (const auto& e : noteOns())
				notes.push_back(e.b);
			return notes;
		}
	};

	void testUpPlaysOnTheGrid()
	{
		Host host(settings(ArpMode::Up));
		host.input = {on(0, 64), on(0, 60), on(0, 67)};
		host.run(6 * g_sixteenth, 512);

		const std::vector<Event> expected =
		{
			on(0, 60), off(3000, 60), on(6000, 64), off(9000, 64), on(12000, 67), off(15000, 67),
			on(18000, 60), off(21000, 60), on(24000, 64), off(27000, 64), on(30000, 67), off(33000, 67)
		};
		EXPECT(host.played == expected);
		EXPECT(host.passed.empty());
	}

	void testBlockSizeDoesNotMatter()
	{
		auto play = [](const uint32_t _blockSize)
		{
			auto s = settings(ArpMode::UpDown);
			s.octaves = 2;
			s.swing = 0.6f;
			Host host(s);
			host.input =
			{
				on(100, 60), on(2000, 64), on(5000, 67), off(40000, 64), off(60000, 60), off(60000, 67),
				on(70001, 72), off(90000, 72)
			};
			host.run(100000, _blockSize);
			return host.played;
		};

		const auto reference = play(512);
		EXPECT(reference.size() > 20);

		for (const uint32_t blockSize : {1u, 7u, 64u, 333u, 4096u, 100000u})
			EXPECT(play(blockSize) == reference);
	}

	void testModes()
	{
		auto sequence = [](const ArpMode _mode, const uint8_t _octaves, const uint64_t _steps)
		{
			auto s = settings(_mode);
			s.octaves = _octaves;
			Host host(s);
			host.input = {on(0, 64), on(0, 60), on(0, 67)};	// struck as E, C, G
			host.run(_steps * g_sixteenth, 512);
			return host.notes();
		};

		using Notes = std::vector<uint8_t>;

		EXPECT(sequence(ArpMode::Up, 1, 6) == Notes({60, 64, 67, 60, 64, 67}));
		EXPECT(sequence(ArpMode::Down, 1, 6) == Notes({67, 64, 60, 67, 64, 60}));
		EXPECT(sequence(ArpMode::UpDown, 1, 8) == Notes({60, 64, 67, 64, 60, 64, 67, 64}));
		EXPECT(sequence(ArpMode::AsPlayed, 1, 6) == Notes({64, 60, 67, 64, 60, 67}));

		EXPECT(sequence(ArpMode::Up, 2, 7) == Notes({60, 64, 67, 72, 76, 79, 60}));
		EXPECT(sequence(ArpMode::Down, 2, 7) == Notes({79, 76, 72, 67, 64, 60, 79}));
		EXPECT(sequence(ArpMode::UpDown, 2, 11) == Notes({60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60}));
		EXPECT(sequence(ArpMode::AsPlayed, 2, 6) == Notes({64, 60, 67, 76, 72, 79}));

		// All notes on every step, an octave higher per step
		EXPECT(sequence(ArpMode::Chord, 2, 3) == Notes({60, 64, 67, 72, 76, 79, 60, 64, 67}));

		const auto random = sequence(ArpMode::Random, 1, 32);
		EXPECT(random.size() == 32);
		for (const auto n : random)
			EXPECT(n == 60 || n == 64 || n == 67);

		// Octaves above the MIDI range are left out
		auto s = settings(ArpMode::Up);
		s.octaves = 3;
		Host high(s);
		high.input = {on(0, 110)};
		high.run(3 * g_sixteenth, 512);
		EXPECT(high.notes() == Notes({110, 122, 110}));
	}

	void testSwing()
	{
		auto s = settings(ArpMode::Up);
		s.swing = 0.75f;
		Host host(s);
		host.input = {on(0, 60)};
		host.run(4 * g_sixteenth, 512);

		// Every odd step starts half a step late, and every note lasts half of its step as swing makes it
		const std::vector<Event> expected =
		{
			on(0, 60), off(4500, 60), on(9000, 60), off(10500, 60),
			on(12000, 60), off(16500, 60), on(21000, 60), off(22500, 60)
		};
		EXPECT(host.played == expected);

		// With extreme swing, the odd step before an even one is short. A key on the even step plays that step only
		s.swing = 0.9f;
		Host extreme(s);
		extreme.input = {on(12000, 60)};
		extreme.run(13000, 512);
		EXPECT(extreme.noteOnTimes() == std::vector<uint64_t>({12000}));
	}

	void testPattern()
	{
		auto s = settings(ArpMode::Up);
		s.pattern =
		{
			{true, 100, 10, 1.0f},	// a fixed velocity, legato, a slow release
			{false},				// a rest
			{true, 0, 64, 0.25f}	// the velocity of the key
		};
		Host host(s);
		host.input = {on(0, 60, 80), on(0, 64, 90)};
		host.run(4 * g_sixteenth, 512);

		// A rest does not use up a note: the step after it plays the next one
		const std::vector<Event> expected =
		{
			on(0, 60, 100), off(6000, 60, 10), on(12000, 64, 90), off(13500, 64), on(18000, 60, 100)
		};
		EXPECT(host.played == expected);
	}

	ArpStep step(const float _duration, const float _length = 0.25f)
	{
		ArpStep s;
		s.duration = _duration;
		s.length = _length;
		return s;
	}

	void testStepDurations()
	{
		// A ratchet, then a step of one and one of two, four steps in all
		auto s = settings(ArpMode::Up);
		s.pattern = {step(0.5f), step(0.5f), step(1.0f), step(2.0f)};
		Host host(s);
		host.input = {on(0, 60)};
		host.run(8 * g_sixteenth, 512);

		EXPECT(host.noteOnTimes() == std::vector<uint64_t>({0, 3000, 6000, 12000, 24000, 27000, 30000, 36000}));

		// Swing moves positions inside a pair of steps too: with 0.75 the first step of a pair takes 1.5 steps and the
		// second one 0.5, so half a step lands at 0.75 and one and a half at 1.75
		s.pattern = {step(0.5f)};
		s.swing = 0.75f;
		Host swung(s);
		swung.input = {on(0, 60)};
		swung.run(4 * g_sixteenth, 512);

		EXPECT(swung.noteOnTimes() == std::vector<uint64_t>({0, 4500, 9000, 10500, 12000, 16500, 21000, 22500}));
	}

	void testVelocityOffset()
	{
		auto velocities = [](const uint8_t _key, const uint8_t _velocity, const int8_t _offset)
		{
			auto s = settings(ArpMode::Up);
			ArpStep st;
			st.velocity = _velocity;
			st.velocityOffset = _offset;
			s.pattern = {st};
			Host host(s);
			host.input = {on(0, 60, _key)};
			host.run(1, 512);
			return host.noteOns().front().c;
		};

		EXPECT(velocities(100, 0, -30) == 70);	// the key's velocity, moved
		EXPECT(velocities(10, 0, -30) == 1);	// never a note off
		EXPECT(velocities(100, 120, 20) == 127);
		EXPECT(velocities(100, 50, 0) == 50);
	}

	void testKeysJoinWhereTheyFit()
	{
		// Up has played 64 when 60 and 67 are struck: it goes on above 64, as the Virus TI does, instead of starting
		// over from the bottom
		Host host(settings(ArpMode::Up));
		host.input = {on(0, 64), on(3000, 60), on(3000, 67)};
		host.run(4 * g_sixteenth, 512);
		EXPECT(host.notes() == std::vector<uint8_t>({64, 67, 60, 64}));

		// As Played keeps its place in a sequence that has the same note twice: 72 is a key and 60's octave
		auto s = settings(ArpMode::AsPlayed);
		s.octaves = 2;
		Host twice(s);
		twice.input = {on(0, 60), on(0, 72)};
		twice.run(6 * g_sixteenth, 512);
		EXPECT(twice.notes() == std::vector<uint8_t>({60, 72, 72, 84, 60, 72}));
	}

	void testDivisions()
	{
		auto stepTimes = [](const ArpDivision _division)
		{
			auto s = settings(ArpMode::Up);
			s.division = _division;
			Host host(s);
			host.input = {on(0, 60)};
			host.run(96000, 512);
			return host.noteOnTimes();
		};

		using Times = std::vector<uint64_t>;

		EXPECT(stepTimes({3, 16}) == Times({0, 18000, 36000, 54000, 72000, 90000}));	// a dotted eighth
		EXPECT(stepTimes({1, 12}).size() == 12);										// eighth triplets, 8000 samples
		EXPECT(stepTimes({1, 12})[11] == 88000);
		EXPECT(stepTimes({1, 1}) == Times({0}));										// a whole note
	}

	void testFreeTempo()
	{
		auto s = settings(ArpMode::Up);
		s.sync = false;
		s.tempo = 60.0f;
		Host host(s);		// plays at 120 BPM, which has to make no difference
		host.input = {on(1234, 60)};
		host.run(40000, 512);

		// A sixteenth at 60 BPM is 12000 samples, and the arpeggio starts with the key
		EXPECT(host.noteOnTimes() == std::vector<uint64_t>({1234, 13234, 25234, 37234}));
	}

	void testHostStopKeepsTheArpeggioGoing()
	{
		Host host(settings(ArpMode::Up));
		host.input = {on(0, 60)};
		host.run(30000, 512);
		host.playing = false;
		host.run(60000, 512);

		const auto times = host.noteOnTimes();
		EXPECT(times.size() == 10);
		for (size_t i = 0; i < times.size(); ++i)
			EXPECT(times[i] == i * g_sixteenth);
	}

	void testHold()
	{
		auto s = settings(ArpMode::Up);
		s.hold = true;
		Host host(s);
		host.input = {on(0, 60), on(0, 64), off(1000, 60), off(1000, 64), on(30000, 67), off(31000, 67)};
		host.run(48000, 512);

		// The released keys keep playing, until a key struck with no key down replaces them
		EXPECT(host.notes() == std::vector<uint8_t>({60, 64, 60, 64, 60, 67, 67, 67}));
	}

	void testNotesFromBeforeTheArpeggiatorReachTheDevice()
	{
		Host host(settings(ArpMode::Off));
		host.input = {on(0, 60), off(20000, 60), on(20000, 62)};
		host.run(10000, 512);
		host.arp.setSettings(settings(ArpMode::Up));
		host.run(30000, 512);

		// The device got the note on of 60 while the arpeggiator was off, so it needs the note off too
		EXPECT(host.passed == std::vector<Event>({on(0, 60), off(20000, 60)}));

		// 62 is struck a third into a step, too late for it: it starts with the next step
		EXPECT(host.noteOns() == std::vector<Event>({on(24000, 62)}));
	}

	void testLateKeys()
	{
		Host host(settings(ArpMode::Up));
		host.input = {on(6002, 60), off(7000, 60), on(20400, 64)};
		host.run(30000, 512);

		// Two samples late still plays the step. 40% into a step waits for the next one
		EXPECT(host.noteOns() == std::vector<Event>({on(6002, 60), on(24000, 64)}));
	}

	void testSwitchingOffEndsNotes()
	{
		auto s = settings(ArpMode::Up);
		s.noteLength = 4.0f;	// two steps
		Host host(s);
		host.input = {on(0, 60)};
		host.run(1024, 512);
		host.arp.setSettings(settings(ArpMode::Off));
		host.run(2048, 512);

		EXPECT(host.played == std::vector<Event>({on(0, 60), off(1024, 60)}));
	}

	void testAllNotesOffStopsTheArpeggio()
	{
		auto s = settings(ArpMode::Up);
		s.hold = true;
		s.noteLength = 1.9f;
		Host host(s);
		host.input = {on(0, 60), {10000, M_CONTROLCHANGE, MC_ALLNOTESOFF, 0}};
		host.run(30000, 512);

		EXPECT(host.played == std::vector<Event>({on(0, 60), off(5700, 60), on(6000, 60), off(10000, 60)}));
		EXPECT(host.passed == std::vector<Event>({{10000, M_CONTROLCHANGE, MC_ALLNOTESOFF, 0}}));
	}

	void testLoopWraps()
	{
		// A host that wraps exactly: the arpeggio runs on as if there was no loop
		Host exact(settings(ArpMode::Up));
		exact.loopEnd = 4.0;
		exact.input = {on(0, 60)};
		exact.run(3 * 96000, 512);

		const auto times = exact.noteOnTimes();
		EXPECT(times.size() == 48);
		for (size_t i = 0; i < times.size(); ++i)
			EXPECT(times[i] == i * g_sixteenth);

		// A host that wraps late, at the next block start, reports the loop start after the step at the loop end has
		// played already. The two are the same step and must not flam
		Host late(settings(ArpMode::Up));
		late.loopEnd = 4.0;
		late.loopCarries = false;
		late.input = {on(0, 60)};
		late.run(3 * 96000, 512);

		const auto lateTimes = late.noteOnTimes();
		EXPECT(lateTimes.size() > 40);
		for (size_t i = 1; i < lateTimes.size(); ++i)
		{
			const auto distance = lateTimes[i] - lateTimes[i - 1];
			EXPECT(distance >= g_sixteenth && distance < g_sixteenth + 512);
		}
	}

	void testTransportInsideTheBlock()
	{
		// Behind a resampler, the transport arrives somewhere inside a device block, and not in every block. The steps
		// land on the same samples as when it comes at every block start
		auto play = [](const uint32_t _blockSize, const int _transportOffset)
		{
			Host host(settings(ArpMode::Up));
			host.transportOffset = _transportOffset;
			host.input = {on(0, 60), on(0, 64), on(0, 67)};
			host.run(12 * g_sixteenth, _blockSize);
			return host.played;
		};

		const auto reference = play(512, 0);
		EXPECT(reference.size() == 24);
		EXPECT(play(1000, 300) == reference);
		EXPECT(play(7, 3) == reference);
	}

	void testWithoutTransport()
	{
		// No host says where it is: the arpeggio starts with the key and runs at its own tempo, 60 BPM here
		auto s = settings(ArpMode::Up);
		s.tempo = 60.0f;
		Host host(s);
		host.transportOffset = -1;
		host.input = {on(1234, 60)};
		host.run(40000, 512);

		EXPECT(host.noteOnTimes() == std::vector<uint64_t>({1234, 13234, 25234, 37234}));
	}

	// A device that plays its arpeggiator in C++. Device::process() hands it the MIDI, the firmware gets the arpeggio
	class ArpDevice final : public Device
	{
	public:
		ArpDevice() : Device(DeviceCreateParams())
		{
			m_arp.setSettings(settings(ArpMode::Up));
		}

		std::vector<Event> m_received;
		size_t m_transports = 0;

		float getSamplerate() const override { return g_samplerate; }
		bool isValid() const override { return true; }
		bool getState(std::vector<uint8_t>&, StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, StateType) override { return false; }
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 0; }

	protected:
		bool hasArpeggiators() const override { return true; }
		void processArpeggiators(std::vector<SMidiEvent>& _midi, std::vector<SMidiEvent>& _out, const uint32_t _sampleCount) override
		{
			m_arp.process(_midi, _out, _sampleCount, getSamplerate());
		}
		void onHostTransport(const SMidiEvent&) override { ++m_transports; }
		void readMidiOut(std::vector<SMidiEvent>&) override {}
		void processAudio(const TAudioInputs&, const TAudioOutputs&, const size_t _samples) override
		{
			m_time += _samples;
		}
		bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>&) override
		{
			// a transport here would be pushed into the firmware's MIDI input
			EXPECT(_ev.type == MidiEventType::Midi);
			m_received.push_back({m_time + _ev.offset, _ev.a, _ev.b, _ev.c});
			return true;
		}

	private:
		Arpeggiator m_arp;
		uint64_t m_time = 0;
	};

	void testDeviceRunsItsArpeggiators()
	{
		ArpDevice device;

		float buffer[512] = {};
		float* const ptr = buffer;
		const TAudioInputs in = {};
		const TAudioOutputs out = {ptr, ptr};
		std::vector<SMidiEvent> midiOut;

		double ppq = 0.0;
		for (int block = 0; block < 24; ++block)
		{
			std::vector<SMidiEvent> midi = {transportEvent(ppq, 120.0f, true, 0)};
			if (block == 0)
				midi.emplace_back(MidiEventSource::Host, M_NOTEON, 60, 100, 0);
			device.process(in, out, 512, midi, midiOut);
			ppq += 512 * 120.0 / (60.0 * g_samplerate);
		}

		// Every transport reached its hook, none the firmware. Instead of the key, the firmware got the arpeggio
		EXPECT(device.m_transports == 24);
		EXPECT(device.m_received == std::vector<Event>({on(0, 60), off(3000, 60), on(6000, 60), off(9000, 60), on(12000, 60)}));
	}

	// The plugin sends the host's transport with every block, everything else happens in the device
	std::vector<Event> playThroughPlugin(const float _hostSamplerate, const uint32_t _blockSize, const int _blocks)
	{
		ArpDevice device;
		Plugin plugin(&device, [](auto* _d) { return _d; });
		plugin.setMidiClockEnabled(false);
		plugin.setHostSamplerate(_hostSamplerate, g_samplerate);
		plugin.setBlockSize(_blockSize);

		plugin.addMidiEvent(SMidiEvent(MidiEventSource::Host, M_NOTEON, 60, 100, 0));

		std::vector<float> left(_blockSize), right(_blockSize);
		TAudioInputs in{};
		TAudioOutputs out{};
		out[0] = left.data();
		out[1] = right.data();

		double ppq = 0.0;
		for (int block = 0; block < _blocks; ++block)
		{
			plugin.process(in, out, _blockSize, 120.0f, ppq, true, true);
			ppq += _blockSize * 120.0 / (60.0 * _hostSamplerate);
		}

		return device.m_received;
	}

	void testPluginPlaysTheDevicesArpeggiator()
	{
		// 35 blocks end just before the fourth step. The key itself never reaches the device, only the arpeggio
		EXPECT(playThroughPlugin(g_samplerate, 512, 35) == std::vector<Event>(
		{
			on(0, 60), off(3000, 60), on(6000, 60), off(9000, 60), on(12000, 60), off(15000, 60)
		}));

		// With a host at twice the device's rate, the transport passes the resampler like any MIDI and keeps the steps
		// a sixteenth apart at the device's rate, within the sample the resampler rounds an event to
		const auto resampled = playThroughPlugin(g_samplerate * 2, 1024, 70);
		std::vector<uint64_t> ons;
		for (const auto& e : resampled)
		{
			if ((e.a & 0xf0) == M_NOTEON)
				ons.push_back(e.time);
		}
		EXPECT(ons.size() >= 3);
		for (size_t i = 1; i < ons.size(); ++i)
		{
			const auto distance = static_cast<int64_t>(ons[i] - ons[i - 1]);
			EXPECT(distance >= 5999 && distance <= 6001);
		}
	}
}

int main()
{
	baseLib::disableErrorDialogs();

	testUpPlaysOnTheGrid();
	testBlockSizeDoesNotMatter();
	testModes();
	testSwing();
	testPattern();
	testStepDurations();
	testVelocityOffset();
	testKeysJoinWhereTheyFit();
	testDivisions();
	testFreeTempo();
	testHostStopKeepsTheArpeggioGoing();
	testHold();
	testNotesFromBeforeTheArpeggiatorReachTheDevice();
	testLateKeys();
	testSwitchingOffEndsNotes();
	testAllNotesOffStopsTheArpeggio();
	testLoopWraps();
	testTransportInsideTheBlock();
	testWithoutTransport();
	testDeviceRunsItsArpeggiators();
	testPluginPlaysTheDevicesArpeggiator();

	std::puts("arpeggiator_test: all passed");
	return 0;
}
