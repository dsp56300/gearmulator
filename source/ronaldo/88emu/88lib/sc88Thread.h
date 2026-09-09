#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dsp56kBase/ringbuffer.h"
#include "synthLib/midiTypes.h"

namespace emu88Lib
{
	class Sc88Thread
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;
		using Render = std::function<SampleFrame()>;
		using SendMidi = std::function<void(const synthLib::SMidiEvent&)>;
		using ReadMidi = std::function<void(std::vector<synthLib::SMidiEvent>&)>;
		using BeforeJob = std::function<void()>;

		Sc88Thread(Render _render, SendMidi _sendMidi, ReadMidi _readMidi, BeforeJob _beforeJob);
		~Sc88Thread();

		void processSamples(uint32_t _count, uint32_t _requiredLatency,
		                    std::vector<synthLib::SMidiEvent>& _midiIn,
		                    std::vector<synthLib::SMidiEvent>& _midiOut);
		bool popSample(SampleFrame& _sample);

	private:
		using TimedMidi = std::pair<uint64_t, synthLib::SMidiEvent>;
		struct ProcessJob
		{
			uint32_t samplesToProcess = 0;
			std::vector<TimedMidi> midiEvents;
		};
		struct OutputFrame
		{
			SampleFrame sample{};
			uint32_t transportGeneration = 0;
		};

		void threadFunc();
		void processJob(ProcessJob& _job);
		void handleTransportDiscontinuity(uint32_t _generation);
		void recoverFromQueueOverflow();
		void silenceActiveChannels();
		void trackMidiActivity(const synthLib::SMidiEvent& _event);
		static bool isTransportBound(const synthLib::SMidiEvent& _event);

		Render m_render;
		SendMidi m_sendMidi;
		ReadMidi m_readMidi;
		BeforeJob m_beforeJob;
		std::unique_ptr<std::thread> m_thread;
		std::atomic<bool> m_exit = false;
		std::atomic<uint64_t> m_droppedSamples = 0;
		std::atomic<bool> m_recoveryRequested = false;
		std::atomic<uint32_t> m_outputGeneration = 0;
		uint32_t m_currentLatency = 0;
		dsp56k::RingBuffer<OutputFrame, 16384, true> m_audioOut;
		std::vector<synthLib::SMidiEvent> m_midiOutput;
		std::mutex m_outputMutex;
		uint64_t m_inSampleOffset = 0;
		std::vector<ProcessJob> m_jobPool;
		dsp56k::RingBuffer<ProcessJob, 32, true> m_pendingJobs;
		uint64_t m_processedSampleOffset = 0;
		uint32_t m_workerGeneration = 0;
		uint64_t m_activeChannels = 0;
		std::vector<synthLib::SMidiEvent> m_tempMidiOut;
		std::vector<TimedMidi> m_tempMidiIn;
	};
}
