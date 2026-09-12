#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace jeLib
{
	/* Tell the pipeline how the caller is scheduled, so its worker threads can
	 * mirror it one priority below. Call from the HOST'S AUDIO THREAD -- that is
	 * the thread whose deadline we must not miss. Safe to call every block; it
	 * does nothing unless the schedule changed, and nothing at all if the caller
	 * is not realtime. */
	void pipelineAdoptHostSchedule();

	class Je8086;

	/* Opt-in parallel ASIC pipeline.
	 *
	 * The JP-8000 chain is four ASICs in series, so it does not parallelise by
	 * splitting a sample across cores; it PIPELINES, one stage per core, each
	 * handing its neighbour the GRAM words that cross the boundary. Throughput
	 * is then bounded by the slowest stage rather than by their sum.
	 *
	 * This exists for CPUs that cannot render the chain in real time on one
	 * core. Measured on a Raspberry Pi 4B at 1.8 GHz: 0.85x serial, 2.01x with
	 * two stages, 3.02x with four. A desktop already clears real time serially
	 * and gains nothing, which is why this is OFF unless a host asks for it.
	 *
	 * Threads, not processes: a plugin cannot fork. Stage-scoped emulator state
	 * is thread_local (see je8086devices.h), and every stage steps a disjoint
	 * set of Asic objects, so the stages share one Je8086 without locking.
	 *
	 * Ownership: stage 0 is the CALLER's thread (the one driving Je8086::step),
	 * which runs the H8S plus ASICs [0, bounds[0]). This object owns the
	 * threads for stages 1..n.
	 */
	class JePipeline
	{
	public:
		static constexpr int MaxStages = 4;
		static constexpr int RingCapacity = 1024;
		static constexpr int RingMask = RingCapacity - 1;
		static constexpr int HandoffCount = 10;	// widest boundary payload
		static constexpr int UcRingCap = 8192;

		/* `_bounds` lists the first ASIC of each stage after the parent, ascending
		 * and within 1..3; {1,2,3} is the four-stage split. Check valid() before
		 * use.
		 *
		 * Stages are deliberately NOT pinned to cores: measured worthless (three
		 * stages pinned vs unpinned rendered 1.8x either way) and pinning is what
		 * breaks a second instance, since the affinity would be chosen per stage
		 * while the process is shared. */
		JePipeline(Je8086& _je, const std::vector<int>& _bounds);
		~JePipeline();

		JePipeline(const JePipeline&) = delete;
		JePipeline& operator=(const JePipeline&) = delete;

		bool valid() const { return m_valid; }
		int numStages() const { return m_numStages; }

		/* Called on the caller's thread from Je8086::step(): moves whatever the
		 * last stage has finished into the caller's sample buffer. Audio must not
		 * be pushed there by a stage thread -- the buffer and the MIDI rate
		 * limiter behind it are not thread safe. */
		/* _waitForOne blocks until a sample is available. DO NOT use it from
		 * step(): a step often advances the H8S without completing a sample at
		 * all -- that is why JeThread loops `while (buffer empty) step()` -- so
		 * waiting there deadlocks. Kept for callers that drive samples directly. */
		void drainAudio(const std::function<void(int32_t, int32_t)>& _sink, bool _waitForOne = false);

		/* Hand over finished audio and bound how far the H8S may run ahead of it.
		 *
		 * Without this the caller's `while (buffer empty) step()` advances the
		 * H8S every time the stages have not finished yet, so the emulator covers
		 * MORE emulated time than the host asked for -- measured at +17%. The
		 * window is in samples: the parent may lead the audio by that much and no
		 * further, which puts total emulated time back where serial has it.
		 *
		 * The window also trades exactness against overlap. At 1 the H8S is never
		 * ahead, which is closest to serial and slowest; larger values let the
		 * stages overlap but inject MIDI up to `window` samples off. */
		void pump(const std::function<void(int32_t, int32_t)>& _sink, int64_t _window);

		/* Hand the caller exactly one sample for every sample it has rendered,
		 * taking audio that is a CONSTANT _latency samples old: silence until the
		 * pipeline has filled, the real stream after that, waiting when a sample
		 * has not arrived yet.
		 *
		 * That constant is what makes the mode exact and reproducible. A window
		 * bounds how far the H8S may lead, but how far it ACTUALLY gets still
		 * depends on thread timing, so the emulator covers a different span of
		 * emulated time each run and MIDI lands in different places. Here the
		 * count delivered is tied to the count rendered and nothing else, so the
		 * H8S sees precisely the serial timeline while the stages stay _latency
		 * samples behind and overlap freely. */
		void deliver(const std::function<void(int32_t, int32_t)>& _sink, int64_t _latency);

		int64_t inFlight() const;

		/* Readbacks of ASICs this thread does not own, refreshed for the H8S. */
		void refreshParentReadbacks();

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;

		Je8086& m_je;
		int m_numStages = 0;
		bool m_valid = false;

		void stageMain(int _stage);
		void installParentHooks(int _firstBound);
	};
}
