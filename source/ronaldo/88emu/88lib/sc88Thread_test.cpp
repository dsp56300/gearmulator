#include "sc88Thread.h"
#include "cpu/common/test_util.hpp"

#include <chrono>
#include <condition_variable>

using namespace emu88Lib;
using namespace synthLib;
using namespace test;

namespace
{
	bool waitFor(const std::function<bool()>& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while(!predicate())
		{
			if(std::chrono::steady_clock::now() >= deadline) return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return true;
	}

	void transportAndTiming()
	{
		std::atomic<int32_t> frames = 0;
		std::mutex mutex;
		std::vector<std::pair<int32_t, SMidiEvent>> received;
		Sc88Thread worker([&] { const auto n = ++frames; return Sc88Thread::SampleFrame{n, -n}; },
			[&](const SMidiEvent& event) { std::lock_guard lock(mutex); received.emplace_back(frames.load(), event); },
			[](auto&) {}, [] {});
		std::vector<SMidiEvent> input, output;
		input.emplace_back(MidiEventSource::Host, 0x90, 60, 100, 3);
		input.emplace_back(MidiEventSource::Host, 0x90, 61, 100, 40);
		worker.processSamples(16, 0, input, output);
		CHECK(waitFor([&] { return frames >= 16; }));
		CHECK(input.empty());
		{
			std::lock_guard lock(mutex);
			CHECK_EQ(received.size(), 1u);
			if(!received.empty()) CHECK_EQ(received.front().first, 3);
		}

		SMidiEvent marker(MidiEventSource::Internal);
		marker.type = MidiEventType::TransportDiscontinuity;
		marker.transportGeneration = 1;
		input.push_back(marker);
		input.emplace_back(MidiEventSource::Host, 0x90, 62, 100);
		input.emplace_back(MidiEventSource::Physical, 0x90, 63, 100);
		input.emplace_back(MidiEventSource::Host, 0x90, 64, 100);
		input.back().transportGeneration = 1;
		worker.processSamples(48, 0, input, output);
		CHECK(waitFor([&] { return frames >= 64; }));
		Sc88Thread::SampleFrame sample;
		CHECK(waitFor([&] { return worker.popSample(sample); }));
		CHECK_EQ(sample.first, 17); // stale audio from the first generation was discarded
		CHECK_EQ(sample.second, -17);
		{
			std::lock_guard lock(mutex);
			CHECK_EQ(received.size(), 4u);
			if(received.size() == 4)
			{
				CHECK_EQ(received[1].second.a, 0xb0);
				CHECK_EQ(received[1].second.b, MC_ALLSOUNDOFF);
				CHECK_EQ(received[2].first, 16);
				CHECK_EQ(received[3].first, 16);
				CHECK_EQ(received[2].second.b, 63);
				CHECK_EQ(received[3].second.b, 64);
			}
		}
	}

	void saturatedQueueShutdown()
	{
		std::mutex mutex;
		std::condition_variable ready;
		bool entered = false, release = false;
		std::atomic<uint32_t> frames = 0;
		auto worker = std::make_unique<Sc88Thread>([&] { ++frames; return Sc88Thread::SampleFrame{}; },
			[](const auto&) {}, [](auto&) {}, [&]
			{
				std::unique_lock lock(mutex);
				entered = true;
				ready.notify_all();
				ready.wait(lock, [&] { return release; });
			});
		std::vector<SMidiEvent> input, output;
		worker->processSamples(1, 0, input, output);
		{
			std::unique_lock lock(mutex);
			CHECK(ready.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
		}
		for(unsigned i = 0; i < 64; ++i) worker->processSamples(1, 0, input, output);
		{
			std::lock_guard lock(mutex);
			release = true;
		}
		ready.notify_all();
		worker.reset(); // destruction must not block on appending to the full job queue
	}
}

int main()
{
	transportAndTiming();
	saturatedQueueShutdown();
	return finish("sc88Thread");
}
