#pragma once

#include <deque>

#include "midiTypes.h"

#include "dsp56kBase/ringbuffer.h"

namespace synthLib
{
	// Carries MIDI events from the audio thread, which writes them before it processes a block, to an emulated device's
	// DSP thread, which takes them once per audio frame. The ring between the two threads does not lock.
	//
	// When the ring is full, further events wait in an overflow on the audio thread instead of blocking it: the frames
	// that empty the ring only come once the audio thread goes on to process the block, so a block that brought more
	// events than the ring holds used to wait forever (BUG-10440, a host setting every parameter when it loads a
	// plugin). Nothing is dropped and the order is kept, the overflow follows as the ring empties.
	class MidiInputQueue
	{
	public:
		static constexpr size_t RingSize = 16384;

		// audio thread
		void push_back(const SMidiEvent& _ev)
		{
			refill();

			if(m_overflow.empty() && m_ring.try_push_back(_ev))
				return;

			m_overflow.push_back(_ev);
		}

		// audio thread, moves the events that wait in the overflow into the ring as far as it has room. Call it before
		// each block
		void refill()
		{
			while(!m_overflow.empty() && m_ring.try_push_back(m_overflow.front()))
				m_overflow.pop_front();
		}

		// DSP thread
		bool empty() const { return m_ring.empty(); }
		const SMidiEvent& front() const { return m_ring.front(); }
		void pop_front() { m_ring.pop_front([](const SMidiEvent&) {}); }	// leaves the event in its slot, nothing to move out

	private:
		dsp56k::RingBuffer<SMidiEvent, RingSize, true> m_ring;
		std::deque<SMidiEvent> m_overflow;
	};
}
