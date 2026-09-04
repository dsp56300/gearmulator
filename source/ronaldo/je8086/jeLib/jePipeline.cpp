#include "jePipeline.h"

#include "je8086.h"
#include "je8086devices.h"

#include "baseLib/os.h"

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <ctime>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#endif

namespace jeLib
{
	namespace
	{
		/* Raise this thread to SCHED_FIFO.
		 *
		 * A host calls process() from a realtime thread -- Ardour uses FIFO 78 --
		 * and jeLib then hands the work to JeThread, which is SCHED_OTHER, and this
		 * pipeline hands it on again to stage threads that were also SCHED_OTHER.
		 * The host's realtime thread therefore BLOCKS ON NON-REALTIME WORKERS, and
		 * whenever a GUI or an X server wants the CPU those workers are scheduled
		 * late and the deadline is missed. Measured in Ardour on a Pi: continuous
		 * underruns with 70% of the CPU idle, which is the signature of priority
		 * inversion rather than of too little compute.
		 *
		 * Stay BELOW the host's own audio thread: we are a producer it waits on,
		 * not a peer, and outranking it only moves the starvation elsewhere.
		 * Silently does nothing without permission (see RLIMIT_RTPRIO). */
		/* What the host's audio thread told us about itself, and a generation so
		 * workers notice. The pipeline is built during boot, long before a host
		 * ever calls process(), so the schedule cannot simply be read at
		 * construction -- it arrives later and every worker adopts it then. */
		struct HostSchedule
		{
			std::atomic<int> policy{-1};
			std::atomic<int> priority{0};
			std::atomic<uint32_t> generation{0};
		};

		inline HostSchedule& hostSchedule()
		{
			static HostSchedule s;
			return s;
		}

		inline bool raiseRealtime()
		{
#ifdef __linux__
			int policy = SCHED_FIFO;
			int prio = 0;

			{
				policy = hostSchedule().policy.load(std::memory_order_relaxed);

				// The host is not realtime, so neither are we. Taking priority
				// uninvited is rude, and pointless if nobody is waiting on us.
				if (policy != SCHED_FIFO && policy != SCHED_RR)
					return false;

				/* One step BELOW the host's audio thread: we are a producer it
				 * waits on, not a peer, and outranking it only moves the
				 * starvation elsewhere. */
				prio = hostSchedule().priority.load(std::memory_order_relaxed) - 1;

				const int lo = sched_get_priority_min(policy);
				if (prio < lo)
					prio = lo;
			}

			if (prio <= 0)
				return false;

			sched_param sp{};
			sp.sched_priority = prio;
			return pthread_setschedparam(pthread_self(), policy, &sp) == 0;
#else
			return false;
#endif
		}

		/* Cheap enough to sit in the sample loop: one relaxed load unless the
		 * host's schedule actually changed. */
		inline void followHostSchedule(uint32_t& _seen)
		{
			const auto gen = hostSchedule().generation.load(std::memory_order_relaxed);
			if (gen == _seen)
				return;
			_seen = gen;
			raiseRealtime();
		}

		/* Spin briefly, then SLEEP. A pure spin is faster on an idle benchmark box
		 * and ruinous on a machine that also has to play the audio: when a real
		 * consumer paces us we are ahead of it most of the time, so the stages sit
		 * in the wait burning whole cores and starving the process that owns the
		 * sound card. Measured with aplay at a 20 ms buffer: spinning gave 12
		 * underruns in 15 s at normal priority and 115 at SCHED_FIFO 60, because a
		 * spinning realtime thread never yields to the consumer at all.
		 *
		 * The back-off is bounded well under one audio block so it cannot become
		 * the thing that misses a deadline. */
		inline void cpuRelax()
		{
#if defined(__aarch64__) || defined(__arm__)
			__asm__ __volatile__("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
			__asm__ __volatile__("pause" ::: "memory");
#endif
		}

		template<typename Pred, typename Stop>
		inline bool spinWait(Pred _pred, Stop _stop)
		{
			/* Sleep length is a real-time trade, not a throughput one. A sample is
			 * 11 us at 88.2 kHz, so a 200 us sleep costs ~18 samples of wake-up on
			 * every handoff; chained across stages that is what eats a small audio
			 * buffer, and it is why this could not go below a 26 ms buffer live
			 * even with two thirds of the CPU idle. Stay sub-sample. */
			constexpr uint32_t s_spins = 4096;
			constexpr uint32_t s_sleepNs = 5'000;

			uint32_t spins = 0;
			while (!_pred())
			{
				if (_stop()) return false;
				if (++spins < s_spins)
				{
					cpuRelax();
				}
				else if (s_sleepNs)
				{
					timespec ts{0, static_cast<long>(s_sleepNs)};
					nanosleep(&ts, nullptr);
				}
			}
			return true;
		}
	}

	struct JePipeline::Impl
	{
		struct Handoff { int32_t gram[HandoffCount]; };
		struct Audio { int32_t left, right; };
		struct UcWrite { uint8_t asic, val; uint16_t addr; uint32_t sample; };

		struct Stage
		{
			int lo = 0, hi = 0;
			std::atomic<bool> ready{false};

			Handoff gramRing[RingCapacity];
			std::atomic<int> gramWrite{0}, gramRead{0};

			UcWrite ucRing[UcRingCap];
			std::atomic<int> ucWrite{0}, ucRead{0};

			std::atomic<int64_t> samplesProduced{0};
		};

		Stage stage[MaxStages];
		std::thread threads[MaxStages];
		std::atomic<bool> shutdown{false};

		Audio audioRing[RingCapacity];
		std::atomic<int> audioWrite{0}, audioRead{0};

		uint8_t readback[4][4] = {};

		std::atomic<int64_t> drained{0};
		int64_t delivered = 0;	// caller's thread only

		static int avail(const std::atomic<int>& _w, const std::atomic<int>& _r)
		{
			int d = _w.load(std::memory_order_acquire) - _r.load(std::memory_order_relaxed);
			if (d < 0) d += RingCapacity * 2;
			return d;
		}

		/* Same discipline for the control-write ring. Indices run modulo 2 * cap so
		 * that a full ring is distinguishable from an empty one; storing them modulo
		 * the capacity itself makes the two states identical and lets a producer
		 * overwrite entries the consumer has not read. Occupancy measured at 14 of
		 * 8192 in practice, so this is a guard rather than a hot path. */
		static int ucAvail(const std::atomic<int>& _w, const std::atomic<int>& _r)
		{
			int d = _w.load(std::memory_order_acquire) - _r.load(std::memory_order_relaxed);
			if (d < 0) d += UcRingCap * 2;
			return d;
		}
	};

	JePipeline::JePipeline(Je8086& _je, const std::vector<int>& _bounds)
		: m_impl(new Impl()), m_je(_je)
	{
		if (_bounds.empty() || _bounds.size() >= MaxStages)
			return;
		for (size_t i = 0; i < _bounds.size(); ++i)
		{
			if (_bounds[i] < 1 || _bounds[i] > 3) return;
			if (i && _bounds[i] <= _bounds[i - 1]) return;
		}

		m_numStages = static_cast<int>(_bounds.size()) + 1;

		auto& impl = *m_impl;
		impl.stage[0].lo = 0;
		impl.stage[0].hi = _bounds[0];
		for (int s = 1; s < m_numStages; ++s)
		{
			impl.stage[s].lo = _bounds[s - 1];
			impl.stage[s].hi = (s < static_cast<int>(_bounds.size())) ? _bounds[s] : 4;
		}

		installParentHooks(_bounds[0]);
		/* This runs on whichever thread drives step() -- JeThread -- and that
		 * thread renders stage 0, so it needs the same treatment as the stages. */
		raiseRealtime();

		for (int s = 1; s < m_numStages; ++s)
			impl.threads[s] = std::thread([this, s] { stageMain(s); });

		/* Stages only need their thread_local state in place before the first
		 * handoff arrives, but waiting here keeps a failure at construction. */
		for (int s = 1; s < m_numStages; ++s)
			while (!impl.stage[s].ready.load(std::memory_order_acquire))
				std::this_thread::yield();

		m_valid = true;
	}

	JePipeline::~JePipeline()
	{
		auto& impl = *m_impl;
		impl.shutdown.store(true, std::memory_order_release);
		for (int s = 1; s < m_numStages; ++s)
			if (impl.threads[s].joinable())
				impl.threads[s].join();

		// leave the emulator in serial mode
		devices::g_je_parallel_mode = 0;
		devices::g_je_stage_lo = 0;
		devices::g_je_stage_hi = 4;
		devices::g_je_gram_produce = nullptr;
		devices::g_je_gram_consume = nullptr;
		devices::g_je_uc_write_forward = nullptr;
		devices::g_je_parent_dummy_audio = true;
	}

	/* Hooks for the caller's thread: it owns ASICs [0, firstBound), publishes
	 * that boundary's handoff to stage 1, and forwards H8S register writes aimed
	 * at ASICs it does not own. */
	void JePipeline::installParentHooks(const int _firstBound)
	{
		auto& impl = *m_impl;

		devices::g_je_split_asic = _firstBound;
		devices::g_je_parallel_mode = 1;
		devices::g_je_stage_lo = 0;
		devices::g_je_stage_hi = _firstBound;
		devices::g_je_parent_dummy_audio = false;	// we drain the stages' real audio ourselves

		auto* next = &impl.stage[1];
		devices::g_je_gram_produce = [this, next](const int32_t* _gram)
		{
			auto& impl2 = *m_impl;
			if (!spinWait([&] { return Impl::avail(next->gramWrite, next->gramRead) < RingCapacity - 1; },
			              [&] { return impl2.shutdown.load(std::memory_order_relaxed); }))
				return;
			const int wi = next->gramWrite.load(std::memory_order_relaxed) & RingMask;
			std::memcpy(next->gramRing[wi].gram, _gram, sizeof(int32_t) * HandoffCount);
			next->gramWrite.store((next->gramWrite.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
			                      std::memory_order_release);
			impl2.stage[0].samplesProduced.fetch_add(1, std::memory_order_release);
		};

		/* The H8S programs ASICs the stages own; the write must land on the same
		 * sample there as here, so it carries the parent's sample index. */
		devices::g_je_uc_write_forward = [this](const int _asic, const uint32_t _addr, const uint8_t _val)
		{
			auto& impl2 = *m_impl;
			for (int s = 1; s < m_numStages; ++s)
			{
				auto& st = impl2.stage[s];
				if (_asic < st.lo || _asic >= st.hi)
					continue;
				if (!spinWait([&] { return Impl::ucAvail(st.ucWrite, st.ucRead) < UcRingCap - 1; },
				              [&] { return impl2.shutdown.load(std::memory_order_relaxed); }))
					return;
				const int wi = st.ucWrite.load(std::memory_order_relaxed) % UcRingCap;
				st.ucRing[wi] = { static_cast<uint8_t>(_asic), _val, static_cast<uint16_t>(_addr),
				                  static_cast<uint32_t>(impl2.stage[0].samplesProduced.load(std::memory_order_relaxed)) };
				st.ucWrite.store((st.ucWrite.load(std::memory_order_relaxed) + 1) % (UcRingCap * 2),
				                 std::memory_order_release);
				break;
			}
		};
	}

	void pipelineAdoptHostSchedule()
	{
#ifdef __linux__
		int policy = 0;
		sched_param sp{};
		if (pthread_getschedparam(pthread_self(), &policy, &sp) != 0)
			return;

		auto& hs = hostSchedule();
		if (hs.policy.load(std::memory_order_relaxed) == policy &&
		    hs.priority.load(std::memory_order_relaxed) == sp.sched_priority)
			return;

		hs.policy.store(policy, std::memory_order_relaxed);
		hs.priority.store(sp.sched_priority, std::memory_order_relaxed);
		hs.generation.fetch_add(1, std::memory_order_release);
#endif
	}

	void JePipeline::stageMain(const int _stage)
	{
		auto& impl = *m_impl;
		auto& st = impl.stage[_stage];
		auto& prev = impl.stage[_stage - 1];
		const bool isLast = (_stage + 1) >= m_numStages;
		const int lo = st.lo, hi = st.hi;

		baseLib::setFlushDenormalsToZero();	// FPCR is per thread
		raiseRealtime();

		devices::g_je_parallel_mode = 2;
		devices::g_je_stage_lo = lo;
		devices::g_je_stage_hi = hi;

		devices::g_je_gram_consume = [this, &st](int32_t* _gram) -> bool
		{
			auto& impl2 = *m_impl;
			if (!spinWait([&] { return Impl::avail(st.gramWrite, st.gramRead) >= 1; },
			              [&] { return impl2.shutdown.load(std::memory_order_relaxed); }))
				return false;
			const int ri = st.gramRead.load(std::memory_order_relaxed) & RingMask;
			std::memcpy(_gram, st.gramRing[ri].gram, sizeof(int32_t) * HandoffCount);
			st.gramRead.store((st.gramRead.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
			                  std::memory_order_release);
			return true;
		};

		if (!isLast)
		{
			auto* next = &impl.stage[_stage + 1];
			devices::g_je_gram_produce = [this, next, &st](const int32_t* _gram)
			{
				auto& impl2 = *m_impl;
				if (!spinWait([&] { return Impl::avail(next->gramWrite, next->gramRead) < RingCapacity - 1; },
				              [&] { return impl2.shutdown.load(std::memory_order_relaxed); }))
					return;
				const int wi = next->gramWrite.load(std::memory_order_relaxed) & RingMask;
				std::memcpy(next->gramRing[wi].gram, _gram, sizeof(int32_t) * HandoffCount);
				next->gramWrite.store((next->gramWrite.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
				                      std::memory_order_release);
				st.samplesProduced.fetch_add(1, std::memory_order_release);
			};
		}
		else
		{
			/* Stage-scoped, not MultiAsic::setPostSample: the object is shared, so
			 * installing it there would also catch the caller's dummy postSample. */
			devices::g_je_stage_audio_out = [this, &st](const int32_t _l, const int32_t _r)
			{
				auto& impl2 = *m_impl;
				if (!spinWait([&] { return Impl::avail(impl2.audioWrite, impl2.audioRead) < RingCapacity - 1; },
				              [&] { return impl2.shutdown.load(std::memory_order_relaxed); }))
					return;
				const int wi = impl2.audioWrite.load(std::memory_order_relaxed) & RingMask;
				impl2.audioRing[wi] = { _l, _r };
				impl2.audioWrite.store((impl2.audioWrite.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
				                       std::memory_order_release);
				st.samplesProduced.fetch_add(1, std::memory_order_release);
			};
		}

		st.ready.store(true, std::memory_order_release);

		auto& asics = m_je.getAsics();
		uint32_t sample = 0;
		uint32_t seenSchedule = 0;

		while (!impl.shutdown.load(std::memory_order_relaxed))
		{
			followHostSchedule(seenSchedule);
			/* Wait for our input handoff AND for the previous stage to have
			 * published this sample, so forwarded register writes stamped with it
			 * have all arrived. */
			if (!spinWait([&] { return Impl::avail(st.gramWrite, st.gramRead) >= 1 &&
			                           prev.samplesProduced.load(std::memory_order_acquire) > static_cast<int64_t>(sample); },
			              [&] { return impl.shutdown.load(std::memory_order_relaxed); }))
				break;

			while (Impl::ucAvail(st.ucWrite, st.ucRead) > 0)
			{
				const int ri = st.ucRead.load(std::memory_order_relaxed) % UcRingCap;
				const auto w = st.ucRing[ri];
				if (w.sample > sample)
					break;
				st.ucRead.store((st.ucRead.load(std::memory_order_relaxed) + 1) % (UcRingCap * 2),
				                std::memory_order_release);
				asics.applyUcWrite(w.asic, w.addr, w.val);
			}

			if (!asics.processSampleChild())
				break;
			++sample;

			for (int a = lo; a < hi; ++a)
				asics.getReadback(a, impl.readback[a]);

		}
	}

	void JePipeline::drainAudio(const std::function<void(int32_t, int32_t)>& _sink, const bool _waitForOne)
	{
		auto& impl = *m_impl;
		if (_waitForOne)
		{
			if (!spinWait([&] { return Impl::avail(impl.audioWrite, impl.audioRead) > 0; },
			              [&] { return impl.shutdown.load(std::memory_order_relaxed); }))
				return;
		}
		while (Impl::avail(impl.audioWrite, impl.audioRead) > 0)
		{
			const int ri = impl.audioRead.load(std::memory_order_relaxed) & RingMask;
			_sink(impl.audioRing[ri].left, impl.audioRing[ri].right);
			impl.drained.fetch_add(1, std::memory_order_relaxed);
			impl.audioRead.store((impl.audioRead.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
			                     std::memory_order_release);
		}
	}

	int64_t JePipeline::inFlight() const
	{
		auto& impl = *m_impl;
		return impl.stage[0].samplesProduced.load(std::memory_order_acquire) -
		       impl.drained.load(std::memory_order_relaxed);
	}

	void JePipeline::pump(const std::function<void(int32_t, int32_t)>& _sink, const int64_t _window)
	{
		auto& impl = *m_impl;
		drainAudio(_sink);
		while (inFlight() >= _window)
		{
			if (!spinWait([&] { return Impl::avail(impl.audioWrite, impl.audioRead) > 0; },
			              [&] { return impl.shutdown.load(std::memory_order_relaxed); }))
				return;
			drainAudio(_sink);
		}
	}

	void JePipeline::deliver(const std::function<void(int32_t, int32_t)>& _sink, const int64_t _latency)
	{
		/* This runs on the thread driving step(), which renders stage 0 and so
		 * needs the same priority as the stages. */
		static thread_local uint32_t seenSchedule = 0;
		followHostSchedule(seenSchedule);

		auto& impl = *m_impl;
		const int64_t produced = impl.stage[0].samplesProduced.load(std::memory_order_acquire);

		while (impl.delivered < produced)
		{
			if (impl.delivered < _latency)
			{
				_sink(0, 0);	// pipeline still filling
			}
			else
			{
				if (!spinWait([&] { return Impl::avail(impl.audioWrite, impl.audioRead) > 0; },
				              [&] { return impl.shutdown.load(std::memory_order_relaxed); }))
					return;
				const int ri = impl.audioRead.load(std::memory_order_relaxed) & RingMask;
				_sink(impl.audioRing[ri].left, impl.audioRing[ri].right);
				impl.audioRead.store((impl.audioRead.load(std::memory_order_relaxed) + 1) % (RingCapacity * 2),
				                     std::memory_order_release);
				impl.drained.fetch_add(1, std::memory_order_relaxed);
			}
			++impl.delivered;
		}
	}

	void JePipeline::refreshParentReadbacks()
	{
		auto& impl = *m_impl;
		auto& asics = m_je.getAsics();
		for (int a = devices::g_je_split_asic; a < 4; ++a)
			devices::MultiAsic::setParentReadback(a, impl.readback[a]);
	}
}
