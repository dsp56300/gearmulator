#include "midiRateLimiter.h"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace
{
	using namespace synthLib;

	SMidiEvent midi(const uint32_t _generation, const uint8_t _status, const uint8_t _data1 = 0,
					const uint8_t _data2 = 0)
	{
		SMidiEvent event(MidiEventSource::Host, _status, _data1, _data2);
		event.transportGeneration = _generation;
		return event;
	}

	void expect(const std::vector<uint8_t>& _actual, const std::initializer_list<uint8_t> _expected)
	{
		if (_actual != std::vector<uint8_t>(_expected))
			std::abort();
	}

	void testRunningStatus()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.disableRateLimit();

		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 1));
		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 2));
		limiter.write(midi(0, M_TIMINGCLOCK));
		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 3));
		limiter.processSample();
		expect(bytes, {M_CONTROLCHANGE, MC_EXPRESSION, 1, MC_EXPRESSION, 2, M_TIMINGCLOCK, MC_EXPRESSION, 3});

		SMidiEvent sysex(MidiEventSource::Host);
		sysex.sysex = {M_STARTOFSYSEX, 1, M_ENDOFSYSEX};
		limiter.write(std::move(sysex));
		limiter.processSample();
		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 4));
		limiter.processSample();
		expect(bytes,
			   {M_CONTROLCHANGE, MC_EXPRESSION, 1, MC_EXPRESSION, 2, M_TIMINGCLOCK, MC_EXPRESSION, 3, M_STARTOFSYSEX, 1,
				M_ENDOFSYSEX, M_CONTROLCHANGE, MC_EXPRESSION, 4});
	}

	void testTransportDropsQueuedEvents()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.disableRateLimit();

		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 10));
		limiter.write(midi(0, M_NOTEON, 60, 100));
		limiter.transportDiscontinuity(1);
		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 11)); // stale worker output
		limiter.write(midi(1, M_NOTEON, 62, 100));
		limiter.processSample();

		expect(bytes, {M_NOTEON, 62, 100});
	}

	void testTransportFinishesPartialMessageThenSilences()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.setSamplerate(1000.0f);
		limiter.setRateLimit(1000.0f); // exactly one byte per processSample()

		limiter.write(midi(0, M_NOTEON, 60, 100));
		limiter.processSample(); // status byte is already on the wire
		limiter.transportDiscontinuity(1);
		for (int i = 0; i < 5; ++i)
			limiter.processSample();

		// The old message must be completed to keep the firmware parser aligned,
		// followed immediately by an explicit silence command for that channel.
		expect(bytes, {M_NOTEON, 60, 100, M_CONTROLCHANGE, MC_ALLSOUNDOFF, 0});
	}

	void testSecondDiscontinuityStillSilences()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.setSamplerate(1000.0f);
		limiter.setRateLimit(1000.0f); // exactly one byte per processSample()

		limiter.write(midi(0, M_NOTEON, 60, 100));
		for (int i = 0; i < 3; ++i)
			limiter.processSample(); // note is on the wire, the channel is ringing

		// Two discontinuities before the queue drains. The first one queues the All Sound Off,
		// the second purges it as belonging to an older generation - so the channel has to still
		// be on the books, or nothing ever silences it.
		limiter.transportDiscontinuity(1);
		limiter.transportDiscontinuity(2);
		for (int i = 0; i < 5; ++i)
			limiter.processSample();

		expect(bytes, {M_NOTEON, 60, 100, M_CONTROLCHANGE, MC_ALLSOUNDOFF, 0});

		// ...and exactly once: the debt is cleared when the message actually goes out.
		limiter.transportDiscontinuity(3);
		for (int i = 0; i < 5; ++i)
			limiter.processSample();
		expect(bytes, {M_NOTEON, 60, 100, M_CONTROLCHANGE, MC_ALLSOUNDOFF, 0});
	}

	SMidiEvent sysex(std::initializer_list<uint8_t> _bytes)
	{
		SMidiEvent event(MidiEventSource::Host);
		event.sysex.assign(_bytes.begin(), _bytes.end());
		return event;
	}

	// The two queues exist so that channel voice traffic is not stuck behind a dump. Nothing
	// else in here queues both at once, so nothing else would notice the order being flipped.
	void testRealtimeOvertakesSysex()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.disableRateLimit();

		limiter.write(sysex({0xf0, 0x01, 0x02, 0xf7}));
		limiter.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 7));
		limiter.processSample();

		expect(bytes, {M_CONTROLCHANGE, MC_EXPRESSION, 7, 0xf0, 0x01, 0x02, 0xf7});

		// ...and the same when the rate limiter is metering the wire one byte at a time.
		std::vector<uint8_t> limited;
		MidiRateLimiter rateLimited([&](const uint8_t _byte) { limited.push_back(_byte); });
		rateLimited.setSamplerate(1000.0f);
		rateLimited.setRateLimit(1000.0f);

		rateLimited.write(sysex({0xf0, 0x01, 0x02, 0xf7}));
		rateLimited.write(midi(0, M_CONTROLCHANGE, MC_EXPRESSION, 7));
		for (int i = 0; i < 8; ++i)
			rateLimited.processSample();

		expect(limited, {M_CONTROLCHANGE, MC_EXPRESSION, 7, 0xf0, 0x01, 0x02, 0xf7});
	}

	void testSysexPauseExpiresWhileIdle()
	{
		std::vector<uint8_t> bytes;
		MidiRateLimiter limiter([&](const uint8_t _byte) { bytes.push_back(_byte); });
		limiter.setSamplerate(100.0f);
		limiter.setRateLimit(100.0f); // exactly one byte per processSample()
		limiter.setSysexPause(0.02f);
		limiter.setSysexPauseLengthThreshold(2);

		limiter.write(sysex({0xf0, 0x01, 0xf7}));
		for (int i = 0; i < 3; ++i)
			limiter.processSample();
		for (int i = 0; i < 3; ++i)
			limiter.processSample(); // the two-sample post-SysEx pause expires while idle

		limiter.write(sysex({0xf0, 0x02, 0xf7}));
		limiter.processSample();
		expect(bytes, {0xf0, 0x01, 0xf7, 0xf0});
	}

} // namespace

int main()
{
	testRunningStatus();
	testTransportDropsQueuedEvents();
	testTransportFinishesPartialMessageThenSilences();
	testSecondDiscontinuityStillSilences();
	testRealtimeOvertakesSysex();
	testSysexPauseExpiresWhileIdle();
	return 0;
}
