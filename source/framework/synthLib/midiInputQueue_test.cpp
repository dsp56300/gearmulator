#include "midiInputQueue.h"

#include <cstdlib>

namespace
{
	using namespace synthLib;

	void check(const bool _condition)
	{
		if(!_condition)
			std::abort();
	}

	SMidiEvent event(const uint32_t _index)
	{
		SMidiEvent e(MidiEventSource::Host, M_CONTROLCHANGE, static_cast<uint8_t>(_index & 0x7f), static_cast<uint8_t>((_index >> 7) & 0x7f));
		e.offset = _index;
		return e;
	}
}

int main()
{
	MidiInputQueue queue;
	uint32_t next = 0;

	// the reader side, checks that the events arrive in the order they were written
	auto read = [&]
	{
		while(!queue.empty())
		{
			check(queue.front().offset == next);
			queue.pop_front();
			++next;
		}
	};

	// A block that brings more events than the ring holds. Writing them must not wait for the reader: on a device, the
	// reader only gets to run once the writer has delivered the block (BUG-10440). If this waits, the test times out
	constexpr uint32_t count = MidiInputQueue::RingSize + 5000;
	for(uint32_t i=0; i<count; ++i)
		queue.push_back(event(i));

	// the ring delivers what it holds, the rest follows once the writer refills it before the next block
	read();
	check(next == MidiInputQueue::RingSize);
	queue.refill();
	read();
	check(next == count);

	// an event written while others still wait in the overflow goes behind them
	for(uint32_t i=0; i<MidiInputQueue::RingSize + 10; ++i)
		queue.push_back(event(count + i));
	read();
	check(next == count + MidiInputQueue::RingSize);
	queue.push_back(event(count + MidiInputQueue::RingSize + 10));
	read();
	check(next == count + MidiInputQueue::RingSize + 11);

	return 0;
}
