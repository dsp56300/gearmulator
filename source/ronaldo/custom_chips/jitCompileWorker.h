#pragma once

// The compile handshake every custom-chip JIT dispatcher uses to keep code generation off the audio thread.
//
// The audio thread kicks a compile whenever the program changes and keeps running the interpreter, which is
// bit-exact with the generated code, until the compile of the latest program lands. That makes the compile
// latency invisible in the output. Kicks during a compile coalesce: the generation advances, the running
// compile's result is discarded when it lands, and one snapshot of the then-current program is issued.
//
// How the code gets compiled follows the back end (jitHost.h):
//   Worker  compile() blocks; it runs on a thread owned here, from a snapshot the audio thread filled.
//   Host    compile() submits to the JavaScript host and returns; poll() reports when it is done.
//   None    nothing is ever compiled and no thread is started.
// setSynchronous(true) compiles on the calling thread instead of the worker, for tests and offline tools.
//
// Result is what one compile produces: a bool for back ends that compile into themselves, or an owning
// handle to generated code. Results that are superseded or retire()d are destroyed on the worker, so
// releasing executable memory never happens on the audio thread.

#include "jitHost.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace chips
{
	enum class JitHostState
	{
		Pending,
		Ready,
		Failed,
	};

	// poll() of a Host back end as a JitHostState. A template, so that back ends of the other kinds, which
	// have no poll(), never instantiate the call.
	template<class Backend>
	JitHostState pollHostBackend(Backend& _backend)
	{
		if constexpr(Backend::Compile == JitCompile::Host)
		{
			using State = typename Backend::CompileState;
			const State state = _backend.poll();
			if(state == State::Pending)
				return JitHostState::Pending;
			return state == State::Ready ? JitHostState::Ready : JitHostState::Failed;
		}
		else
		{
			(void)_backend;
			return JitHostState::Failed;
		}
	}

	// Snapshot is filled on the audio thread by the callable handed to kick()/poll() and read by the compile
	// function. Two of them are default-constructed, so that is where storage is sized; they are swapped
	// rather than copied, so a fill must overwrite everything the compile reads.
	template<JitCompile Mode, class Snapshot, class Result = bool>
	class JitCompileWorker
	{
	public:
		using CompileFunc = std::function<Result(const Snapshot&)>;
		using HostPollFunc = std::function<JitHostState()>;

		static constexpr bool Available = Mode != JitCompile::None;

		// Call on a thread that may block: the worker thread is started here. _hostPoll is used in Host mode
		// only, where _compile submits and its result is ignored.
		explicit JitCompileWorker(CompileFunc _compile, HostPollFunc _hostPoll = {})
			: m_compile(std::move(_compile))
			, m_hostPoll(std::move(_hostPoll))
		{
			startThread();
		}

		~JitCompileWorker()
		{
			stopThread();
		}

		JitCompileWorker(const JitCompileWorker&) = delete;
		JitCompileWorker& operator=(const JitCompileWorker&) = delete;

		// Joins the worker before switching to synchronous compiles, completing a request it had not started.
		void setSynchronous(const bool _synchronous)
		{
			if(m_synchronous == _synchronous)
				return;
			m_synchronous = _synchronous;
			if(!_synchronous)
			{
				startThread();
				return;
			}
			stopThread();
			if(m_inFlight && m_done.load(std::memory_order_acquire) != m_issued)
			{
				std::swap(m_request, m_work);
				compileWork(m_issued);
			}
		}
		bool synchronous() const { return m_synchronous; }

		bool inFlight() const { return m_inFlight; }

		// The program changed: compile it, or coalesce with the compile in flight. _fill(Snapshot&) captures the
		// current program, either now or when the compile in flight lands.
		template<class Fill>
		void kick(Fill&& _fill)
		{
			if constexpr(!Available)
			{
				(void)_fill;
				return;
			}
			++m_kickGeneration;
			if(m_inFlight)
			{
				m_rekickPending = true;
				return;
			}
			m_inFlight = true;
			issue(_fill);
		}

		// Call regularly from the audio thread. Returns true once the compile of the latest kicked program has
		// landed and moves its result to _result. _fill is used if a coalesced kick has to be issued now.
		template<class Fill>
		bool poll(Fill&& _fill, Result& _result)
		{
			if(!m_inFlight)
				return false;
			if constexpr(Mode == JitCompile::Host)
			{
				// One compile at a time, so what lands is what was issued last.
				const JitHostState state = m_hostPoll();
				if(state == JitHostState::Pending)
					return false;
				m_result = Result(state == JitHostState::Ready);
				m_done.store(m_issued, std::memory_order_release);
			}
			const uint64_t done = m_done.load(std::memory_order_acquire);
			if(done != m_issued)
				return false;
			if(m_rekickPending)
			{
				m_rekickPending = false;
				issue(_fill);
				return false;
			}
			if(done != m_kickGeneration)
				return false;
			m_inFlight = false;
			_result = std::move(m_result);
			return true;
		}

		// Hands a result the caller no longer uses to the worker for destruction.
		void retire(Result&& _result)
		{
			if(!m_thread.joinable())
			{
				Result dead(std::move(_result));
				return;
			}
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_retired.push_back(std::move(_result));
			}
			m_cv.notify_one();
		}

	private:
		template<class Fill>
		void issue(Fill& _fill)
		{
			const uint64_t generation = m_kickGeneration;
			m_issued = generation;
			if constexpr(Mode == JitCompile::Host)
			{
				// The back end reads the snapshot only while it emits, which is before the submit returns.
				_fill(m_work);
				m_compile(m_work);
			}
			else
			{
				if(m_synchronous)
				{
					_fill(m_work);
					compileWork(generation);
					return;
				}
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					_fill(m_request);
					m_requestGeneration = generation;
				}
				m_cv.notify_one();
			}
		}

		// The audio thread reads m_result only after m_done carries the generation it issued last and issues
		// the next request only after that, so m_result needs no lock.
		void compileWork(const uint64_t _generation)
		{
			Result result{};
			try
			{
				result = m_compile(m_work);
			}
			catch(...)
			{
				result = Result{};
			}
			std::swap(result, m_result);
			m_done.store(_generation, std::memory_order_release);
		}

		void startThread()
		{
			if constexpr(Mode == JitCompile::Worker)
			{
				if(m_synchronous || m_thread.joinable())
					return;
				m_exit = false;
				m_workGeneration = m_requestGeneration;	// requests from before a synchronous phase are done
				m_thread = std::thread([this] { run(); });
			}
		}

		void stopThread()
		{
			if(!m_thread.joinable())
				return;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_exit = true;
			}
			m_cv.notify_one();
			m_thread.join();
		}

		void run()
		{
			std::vector<Result> retired;
			std::unique_lock<std::mutex> lock(m_mutex);
			for(;;)
			{
				m_cv.wait(lock, [&] { return m_exit || m_requestGeneration != m_workGeneration || !m_retired.empty(); });
				if(m_exit)
					return;
				retired.swap(m_retired);
				const uint64_t generation = m_requestGeneration;
				const bool compile = generation != m_workGeneration;
				if(compile)
				{
					std::swap(m_request, m_work);
					m_workGeneration = generation;
				}
				lock.unlock();

				retired.clear();
				if(compile)
					compileWork(generation);

				lock.lock();
			}
		}

		CompileFunc m_compile;
		HostPollFunc m_hostPoll;

		// Audio thread.
		uint64_t m_kickGeneration = 0;
		uint64_t m_issued = 0;
		bool m_inFlight = false;
		bool m_rekickPending = false;
		bool m_synchronous = false;

		// Handoff, under m_mutex.
		Snapshot m_request;
		uint64_t m_requestGeneration = 0;
		std::vector<Result> m_retired;
		bool m_exit = false;

		// Worker, or the audio thread when synchronous.
		Snapshot m_work;
		uint64_t m_workGeneration = 0;
		Result m_result{};
		std::atomic<uint64_t> m_done{0};

		std::mutex m_mutex;
		std::condition_variable m_cv;
		std::thread m_thread;
	};
}
