#pragma once

#include <deque>

#include "baseLib/semaphore.h"

#include "dsp56kEmu/ringbuffer.h"

#include "synthLib/midiTypes.h"

namespace jeLib
{
	class Je8086;

	class JeThread
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>; // left, right

		JeThread(Je8086& _je8086);
		~JeThread();

		void processSamples(uint32_t _count, uint32_t _requiredLatency, std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut);

		auto& getSampleBuffer() { return m_audioOut; }

	private:
		using MidiEvent = std::pair<uint64_t, synthLib::SMidiEvent>;
		struct ProcessJob
		{
			uint32_t samplesToProcess = 0;
			std::vector<MidiEvent> midiEvents;
		};

		void threadFunc();

		Je8086& m_je8086;

		std::unique_ptr<std::thread> m_thread;

		bool m_exit = false;

		uint32_t m_currentLatency = 0;

		dsp56k::RingBuffer<SampleFrame, 16384, true> m_audioOut;

		std::vector<synthLib::SMidiEvent> m_midiOutput;

		std::mutex m_mutex;

		uint64_t m_inSampleOffset = 0;

		std::vector<ProcessJob> m_jobPool;
		dsp56k::RingBuffer<ProcessJob, 32, true> m_pendingJobs;
	};
}
