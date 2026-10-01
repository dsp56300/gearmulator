// rLib::BoardThread against a single-threaded model of the per-device worker threads it replaced.

#include "common/boardThread.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		if(!_ok)
			++g_failures;
		std::printf("  [%s] %s\n", _ok ? "ok" : "FAIL", _what);
	}

	struct Applied
	{
		uint32_t id;
		uint64_t sample;
		bool operator==(const Applied& _o) const { return id == _o.id && sample == _o.sample; }
	};

	// The frame is the index of the sample, MIDI in is logged with the sample it lands on and every 37th sample
	// emits one MIDI out event, both taken from the sample index
	class FakeBoard
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		explicit FakeBoard(const uint32_t _spin = 0) : m_spin(_spin) {}

		SampleFrame renderSample()
		{
			if(m_inside.exchange(true))
				m_concurrent = true;
			for(volatile uint32_t i = 0; i < m_spin; i = i + 1) {}
			const auto s = m_sample++;
			m_inside = false;
			return {static_cast<int32_t>(s), -static_cast<int32_t>(s)};
		}

		void addMidiEvent(const synthLib::SMidiEvent& _e)
		{
			applied.push_back({static_cast<uint32_t>(_e.a) | (static_cast<uint32_t>(_e.b) << 8) | (static_cast<uint32_t>(_e.c) << 16), m_sample});
		}

		void readMidiOut(std::vector<synthLib::SMidiEvent>& _out)
		{
			if(m_sample % 37 == 0 && m_sample != m_lastOut)
			{
				m_lastOut = m_sample;
				_out.emplace_back(synthLib::MidiEventSource::Device, 0x90, static_cast<uint8_t>(m_sample & 0x7f), static_cast<uint8_t>((m_sample >> 7) & 0x7f));
			}
		}

		std::vector<Applied> applied;
		bool concurrent() const { return m_concurrent; }

	private:
		const uint32_t m_spin;
		uint64_t m_sample = 0;
		uint64_t m_lastOut = ~0ull;
		std::atomic<bool> m_inside{false};
		std::atomic<bool> m_concurrent{false};
	};

	// the per-device threads BoardThread replaced (jeLib::JeThread and the like) with the worker taken out: every job runs inline
	class Reference
	{
	public:
		using SampleFrame = FakeBoard::SampleFrame;

		explicit Reference(FakeBoard& _board) : m_board(_board) {}

		void process(const uint32_t _count, const uint32_t _requiredLatency, std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut, std::vector<SampleFrame>& _frames)
		{
			for(auto& e : _midiIn)
				m_midi.emplace_back(e.offset + _requiredLatency + m_inSampleOffset, e);
			_midiIn.clear();

			uint32_t samples = 0;
			m_inSampleOffset += _count;
			while(_requiredLatency > m_currentLatency)
			{
				++samples;
				++m_currentLatency;
			}
			for(size_t i = 0; i < _count; ++i)
			{
				if(m_currentLatency > _requiredLatency)
					--m_currentLatency;
				else
					++samples;
			}

			for(uint32_t i = 0; i < samples; ++i)
			{
				for(auto it = m_midi.begin(); it != m_midi.end();)
				{
					if(it->first <= m_processed)
					{
						m_board.addMidiEvent(it->second);
						it = m_midi.erase(it);
					}
					else
					{
						++it;
					}
				}
				m_audio.push_back(m_board.renderSample());
				++m_processed;
				m_board.readMidiOut(_midiOut);
			}

			_frames.clear();
			for(uint32_t i = 0; i < _count; ++i)
			{
				_frames.push_back(m_audio.front());
				m_audio.pop_front();
			}
		}

	private:
		FakeBoard& m_board;
		uint32_t m_currentLatency = 0;
		uint64_t m_inSampleOffset = 0;
		uint64_t m_processed = 0;
		std::vector<std::pair<uint64_t, synthLib::SMidiEvent>> m_midi;
		std::deque<SampleFrame> m_audio;
	};

	struct Block
	{
		uint32_t count;
		uint32_t latency;
		std::vector<synthLib::SMidiEvent> midi;
	};

	struct Result
	{
		std::vector<FakeBoard::SampleFrame> audio;
		std::vector<Applied> applied;
		std::vector<synthLib::SMidiEvent> midiOut;
		bool concurrent = false;
	};

	// ends by removing the latency and a block that runs synchronously, so every job has run and all MIDI out has
	// been collected
	std::vector<Block> makeScenario(const uint32_t _seed, const size_t _blocks, const uint32_t _maxLatency, const uint32_t _maxBlock)
	{
		std::mt19937 rng(_seed);
		std::vector<Block> blocks;
		uint32_t latency = 0;
		uint32_t id = 0;

		for(size_t b = 0; b < _blocks; ++b)
		{
			Block block;
			const auto r = rng() % 16;
			block.count = r == 0 ? 0 : 1 + rng() % _maxBlock;
			if(rng() % 8 == 0)
				latency = rng() % 3 == 0 ? 0 : rng() % (_maxLatency + 1);
			block.latency = latency;
			const auto events = rng() % 4;
			for(uint32_t e = 0; e < events; ++e, ++id)
				block.midi.emplace_back(synthLib::MidiEventSource::Host, static_cast<uint8_t>(id & 0x7f), static_cast<uint8_t>((id >> 7) & 0x7f), static_cast<uint8_t>((id >> 14) & 0x7f), block.count ? rng() % block.count : 0);
			blocks.push_back(std::move(block));
		}
		blocks.push_back({_maxLatency + 64, 0, {}});
		blocks.push_back({64, 0, {}});
		return blocks;
	}

	Result runReference(const std::vector<Block>& _blocks)
	{
		FakeBoard board;
		Reference ref(board);
		Result res;
		std::vector<FakeBoard::SampleFrame> frames;
		for(const auto& b : _blocks)
		{
			auto midi = b.midi;
			ref.process(b.count, b.latency, midi, res.midiOut, frames);
			res.audio.insert(res.audio.end(), frames.begin(), frames.end());
		}
		res.applied = board.applied;
		return res;
	}

	Result runThread(const std::vector<Block>& _blocks, const uint32_t _spin = 0)
	{
		FakeBoard board(_spin);
		Result res;
		{
			rLib::BoardThread<FakeBoard> thread(board, "test");
			for(const auto& b : _blocks)
			{
				auto midi = b.midi;
				const auto* frames = thread.process(b.count, b.latency, midi, res.midiOut);
				res.audio.insert(res.audio.end(), frames, frames + b.count);
			}
		}
		res.applied = board.applied;
		res.concurrent = board.concurrent();
		return res;
	}

	bool sameMidiOut(const std::vector<synthLib::SMidiEvent>& _a, const std::vector<synthLib::SMidiEvent>& _b)
	{
		if(_a.size() != _b.size())
			return false;
		for(size_t i = 0; i < _a.size(); ++i)
		{
			if(_a[i].a != _b[i].a || _a[i].b != _b[i].b || _a[i].c != _b[i].c)
				return false;
		}
		return true;
	}

	bool sameResult(const Result& _a, const Result& _b)
	{
		return _a.audio == _b.audio && _a.applied == _b.applied && sameMidiOut(_a.midiOut, _b.midiOut) && !_a.concurrent && !_b.concurrent;
	}

	void testFixedLatency(const uint32_t _latency)
	{
		constexpr uint32_t N = 128;
		std::vector<Block> blocks;
		for(uint32_t k = 0; k < 200; ++k)
		{
			Block b{N, _latency, {}};
			if(k % 3 == 0)
				b.midi.emplace_back(synthLib::MidiEventSource::Host, static_cast<uint8_t>(k & 0x7f), static_cast<uint8_t>(k >> 7), 0, (k * 37) % N);
			blocks.push_back(std::move(b));
		}

		blocks.push_back({_latency + N, 0, {}});
		blocks.push_back({N, 0, {}});

		const auto res = runThread(blocks);

		bool audioOk = res.audio.size() == 202 * N + _latency;
		for(size_t i = 0; audioOk && i < res.audio.size(); ++i)
			audioOk = res.audio[i].first == static_cast<int32_t>(i);

		bool midiOk = res.applied.size() == (200 + 2) / 3;
		for(size_t i = 0; midiOk && i < res.applied.size(); ++i)
		{
			const auto k = static_cast<uint32_t>(i * 3);
			midiOk = res.applied[i].id == ((k & 0x7f) | ((k >> 7) << 8)) && res.applied[i].sample == uint64_t(k) * N + (k * 37) % N + _latency;
		}

		char name[128];
		std::snprintf(name, sizeof(name), "latency %u: output sample i is board sample i", _latency);
		check(audioOk, name);
		std::snprintf(name, sizeof(name), "latency %u: MIDI at block k offset o lands on board sample k*N+o+L", _latency);
		check(midiOk, name);
	}
}

int main()
{
	std::atomic<bool> finished{false};
	std::thread watchdog([&finished]
	{
		for(int i = 0; i < 600 && !finished; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if(!finished)
		{
			std::printf("FAIL: timeout, deadlock?\n");
			std::fflush(stdout);
			std::_Exit(2);
		}
	});

	std::printf("fixed latency\n");
	for(const uint32_t l : {0u, 1u, 100u, 128u, 1000u, 16384u})
		testFixedLatency(l);

	std::printf("latency 0: synchronous path equals the threaded path\n");
	{
		std::vector<Block> sync, threaded;
		std::mt19937 rng(7);
		uint32_t id = 0;
		for(uint32_t k = 0; k < 300; ++k)
		{
			Block b{1 + rng() % 256, 0, {}};
			for(uint32_t e = rng() % 3; e > 0; --e, ++id)
				b.midi.emplace_back(synthLib::MidiEventSource::Host, static_cast<uint8_t>(id & 0x7f), static_cast<uint8_t>(id >> 7), 0, rng() % b.count);
			sync.push_back(b);
		}
		// a latency of 0 on an idle worker renders synchronously, the same stream with 1 sample of latency runs
		// on the worker, the frames it returns are the synchronous ones shifted by one
		threaded = sync;
		for(auto& b : threaded)
			b.latency = 1;
		const auto a = runThread(sync);
		const auto b = runThread(threaded);
		const auto ra = runReference(sync);
		bool shifted = a.audio.size() == b.audio.size() && a.audio == ra.audio;
		for(size_t i = 0; shifted && i < a.audio.size(); ++i)
			shifted = a.audio[i] == b.audio[i];
		bool midiShifted = a.applied.size() == b.applied.size();
		for(size_t i = 0; midiShifted && i < a.applied.size(); ++i)
			midiShifted = a.applied[i].id == b.applied[i].id && a.applied[i].sample + 1 == b.applied[i].sample;
		check(sameResult(a, ra), "synchronous path matches the reference");
		check(shifted && midiShifted, "threaded path renders the same frames, MIDI one sample later");
	}

	std::printf("random latency changes against the reference\n");
	{
		bool ok = true;
		bool concurrent = false;
		for(uint32_t seed = 1; seed <= 200; ++seed)
		{
			const auto maxLatency = seed % 4 == 0 ? 16384u : 700u;
			const auto maxBlock = seed % 5 == 0 ? 4096u : 300u;
			const auto blocks = makeScenario(seed, 300, maxLatency, maxBlock);
			const auto ref = runReference(blocks);
			const auto res = runThread(blocks, seed % 3 == 0 ? 50 : 0);
			concurrent |= res.concurrent;
			if(!sameResult(ref, res))
			{
				std::printf("  mismatch, seed %u: audio %d applied %d (%zu/%zu) midiOut %d (%zu/%zu)\n", seed, ref.audio == res.audio, ref.applied == res.applied, ref.applied.size(), res.applied.size(), sameMidiOut(ref.midiOut, res.midiOut), ref.midiOut.size(), res.midiOut.size());
				ok = false;
			}
		}
		check(ok, "audio, MIDI in and MIDI out identical to the reference for 200 random scenarios");
		check(!concurrent, "the board is never rendered by two threads at once");
	}

	std::printf("blocks larger than the ring\n");
	{
		std::vector<Block> blocks{{100000, 16384, {}}, {70000, 0, {}}, {50000, 5000, {}}, {5128, 0, {}}, {128, 0, {}}};
		check(sameResult(runReference(blocks), runThread(blocks)), "no deadlock, identical to the reference");
	}

	std::printf("latency toggling between 0 and 1 (synchronous right after threaded)\n");
	{
		std::vector<Block> blocks;
		for(uint32_t k = 0; k < 20000; ++k)
			blocks.push_back({32, (k & 1) ? 1u : 0u, {}});
		for(int i = 0; i < 3; ++i)
			blocks.push_back({32, 0, {}});
		const auto res = runThread(blocks, 20);
		check(!res.concurrent && res.audio == runReference(blocks).audio, "no concurrent rendering, identical to the reference");
	}

	finished = true;
	watchdog.join();

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
