#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "dsp56kBase/audioworkgroup.h"
#include "dsp56kBase/threadtools.h"

#include "synthLib/midiTypes.h"

namespace rLib
{
	// Runs a board on a worker thread. TBoard provides SampleFrame renderSample(), addMidiEvent(const SMidiEvent&)
	// and readMidiOut(std::vector<SMidiEvent>&).
	//
	// Latency is added by letting the board run ahead of the host, MIDI is scheduled that many samples later so it
	// lands on the same board sample for any latency. Without latency and with the worker idle, the calling thread
	// renders the board itself.
	template<typename TBoard>
	class BoardThread
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>;

		BoardThread(TBoard& _board, const char* _threadName) : m_board(_board), m_threadName(_threadName), m_ring(RingSize)
		{
			m_thread = std::thread([this] { threadFunc(); });
		}

		~BoardThread()
		{
			{
				std::lock_guard lock(m_mutex);
				m_exit = true;
			}
			m_workerCv.notify_one();
			m_thread.join();
		}

		BoardThread(const BoardThread&) = delete;
		BoardThread& operator=(const BoardThread&) = delete;

		// Returns _count frames, valid until the next call. MIDI output of the board is appended to _midiOut
		const SampleFrame* process(const uint32_t _count, const uint32_t _requiredLatency, std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
		{
			// an event due before its job starts is applied at the job start, behind the ones already pending
			const uint64_t jobStart = m_scheduledSamples;

			for (auto& e : _midiIn)
			{
				const uint64_t offset = e.offset + _requiredLatency + m_inSampleOffset;
				m_newMidi.emplace_back(std::max(offset, jobStart), std::move(e));
			}
			_midiIn.clear();

			m_inSampleOffset += _count;

			uint32_t samples;

			if (_requiredLatency > m_currentLatency)
			{
				samples = _count + _requiredLatency - m_currentLatency;
				m_currentLatency = _requiredLatency;
			}
			else
			{
				const auto drop = std::min(m_currentLatency - _requiredLatency, _count);
				m_currentLatency -= drop;
				samples = _count - drop;
			}

			m_scheduledSamples += samples;

			if (m_output.size() < _count)
				m_output.resize(_count);

			std::unique_lock lock(m_mutex);

			if (m_currentLatency == 0 && m_requested == m_done)
			{
				// the worker is idle, the ring holds what is left of a latency that has been removed
				takeMidi(m_incomingMidi);
				takeMidi(m_newMidi);
				const auto buffered = static_cast<uint32_t>(m_ringWrite - m_ringRead);
				readRing(m_output.data(), buffered);
				lock.unlock();

				render(m_output.data() + buffered, samples);

				lock.lock();
				m_requested = m_done = m_processed;
				publishMidiOut();
			}
			else
			{
				append(m_incomingMidi, m_newMidi);
				m_requested += samples;
				m_workerCv.notify_one();

				for (uint32_t done = 0; done < _count;)
				{
					m_consumerCv.wait(lock, [this] { return m_ringWrite != m_ringRead; });
					const auto n = static_cast<uint32_t>(std::min<uint64_t>(m_ringWrite - m_ringRead, _count - done));
					readRing(m_output.data() + done, n);
					done += n;
					m_workerCv.notify_one();
				}
			}

			append(_midiOut, m_midiOutput);

			return m_output.data();
		}

	private:
		static constexpr uint64_t RingSize = 32768;
		static constexpr uint64_t RingMask = RingSize - 1;
		static constexpr uint64_t ChunkSize = 256;

		using MidiEvent = std::pair<uint64_t, synthLib::SMidiEvent>;	// board sample, event

		template<typename T> static void append(std::vector<T>& _dst, std::vector<T>& _src)
		{
			if (_dst.empty())
			{
				std::swap(_dst, _src);
			}
			else
			{
				_dst.insert(_dst.end(), std::make_move_iterator(_src.begin()), std::make_move_iterator(_src.end()));
				_src.clear();
			}
		}

		void threadFunc()
		{
			dsp56k::ThreadTools::setCurrentThreadName(m_threadName);
			dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);

			dsp56k::AudioWorkgroup::Member workgroup;

			std::unique_lock lock(m_mutex);

			while (true)
			{
				m_workerCv.wait(lock, [this] { return m_exit || m_requested > m_done; });

				if (m_exit)
					return;

				const auto target = m_requested;
				takeMidi(m_incomingMidi);

				while (m_processed < target)
				{
					m_workerCv.wait(lock, [this] { return m_exit || m_ringWrite - m_ringRead < RingSize; });

					if (m_exit)
						return;

					const auto write = m_ringWrite;
					const auto n = std::min({RingSize - (write - m_ringRead), target - m_processed, ChunkSize});

					lock.unlock();

					workgroup.update();

					const auto first = std::min(n, RingSize - (write & RingMask));
					render(&m_ring[write & RingMask], first);
					render(m_ring.data(), n - first);

					lock.lock();

					m_ringWrite += n;
					publishMidiOut();

					if (m_processed == target)
						m_done = target;

					m_consumerCv.notify_one();
				}
			}
		}

		void readRing(SampleFrame* _dst, const uint32_t _count)
		{
			for (uint32_t i = 0; i < _count; ++i)
				_dst[i] = m_ring[(m_ringRead + i) & RingMask];
			m_ringRead += _count;
		}

		// keeps m_pendingMidi sorted by board sample, an event goes behind all others due at the same sample
		void takeMidi(std::vector<MidiEvent>& _src)
		{
			for (auto& e : _src)
			{
				if (m_pendingMidi.empty() || m_pendingMidi.back().first <= e.first)
				{
					m_pendingMidi.emplace_back(std::move(e));
					continue;
				}

				const auto it = std::upper_bound(m_pendingMidi.begin(), m_pendingMidi.end(), e.first, [](const uint64_t _s, const MidiEvent& _e)
				{
					return _s < _e.first;
				});
				m_pendingMidi.insert(it, std::move(e));
			}
			_src.clear();
		}

		void render(SampleFrame* _dst, const uint64_t _count)
		{
			size_t midiRead = 0;

			for (uint64_t i = 0; i < _count; ++i)
			{
				while (midiRead < m_pendingMidi.size() && m_pendingMidi[midiRead].first <= m_processed)
					m_board.addMidiEvent(m_pendingMidi[midiRead++].second);

				_dst[i] = m_board.renderSample();
				++m_processed;

				m_board.readMidiOut(m_renderMidiOut);
			}

			if (midiRead)
				m_pendingMidi.erase(m_pendingMidi.begin(), m_pendingMidi.begin() + static_cast<ptrdiff_t>(midiRead));
		}

		void publishMidiOut()
		{
			if (!m_renderMidiOut.empty())
				append(m_midiOutput, m_renderMidiOut);
		}

		TBoard& m_board;
		const char* const m_threadName;

		std::thread m_thread;

		// audio thread
		uint32_t m_currentLatency = 0;
		uint64_t m_inSampleOffset = 0;
		uint64_t m_scheduledSamples = 0;
		std::vector<MidiEvent> m_newMidi;
		std::vector<SampleFrame> m_output;

		// whoever renders: the worker, or the audio thread while the worker is idle
		uint64_t m_processed = 0;
		std::vector<MidiEvent> m_pendingMidi;
		std::vector<synthLib::SMidiEvent> m_renderMidiOut;

		// guarded by m_mutex. The ring region between write and read + RingSize is written by the renderer unlocked
		std::mutex m_mutex;
		std::condition_variable m_workerCv;
		std::condition_variable m_consumerCv;
		bool m_exit = false;
		uint64_t m_requested = 0;
		uint64_t m_done = 0;
		std::vector<MidiEvent> m_incomingMidi;
		std::vector<synthLib::SMidiEvent> m_midiOutput;
		std::vector<SampleFrame> m_ring;
		uint64_t m_ringWrite = 0;
		uint64_t m_ringRead = 0;
	};
}
