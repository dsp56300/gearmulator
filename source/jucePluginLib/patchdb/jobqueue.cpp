#include "jobqueue.h"

#include <shared_mutex>

#include "dsp56kEmu/threadtools.h"

namespace pluginLib::patchDB
{
	JobQueue::JobQueue(std::string _name, const bool _start/* = true*/, const dsp56k::ThreadPriority& _prio/* = dsp56k::ThreadPriority::Normal*/, const uint32_t _threadCount/* = 1*/)
	: m_name(std::move(_name))
	, m_threadPriority(_prio)
	, m_threadCount(_threadCount)
	{
		if (_start)
			start();
	}

	JobQueue::~JobQueue()
	{
		destroy();
	}

	void JobQueue::start()
	{
		if (!m_threads.empty())
			return;

		m_destroy = false;

		m_threads.reserve(m_threadCount);

		for(size_t i=0; i<m_threadCount; ++i)
		{
			size_t idx = i;
			m_threads.emplace_back(new std::thread([this, idx]
			{
				if (!m_name.empty())
					dsp56k::ThreadTools::setCurrentThreadName(m_name + std::to_string(idx));
				dsp56k::ThreadTools::setCurrentThreadPriority(m_threadPriority);
				threadFunc();
			}));
		}
	}

	void JobQueue::destroy()
	{
		if (m_destroy)
			return;

		{
			std::unique_lock lock(m_mutexFuncs);
			m_funcs.emplace_back([this]
			{
				m_destroy = true;
			});
		}

		m_cv.notify_all();

		for (const auto& thread : m_threads)
			thread->join();
		m_threads.clear();

		m_funcs.clear();
		m_emptyCv.notify_all();
	}

	void JobQueue::add(std::function<void()>&& _func)
	{
		{
			std::unique_lock lock(m_mutexFuncs);
			m_funcs.emplace_back(std::move(_func));
		}
		m_cv.notify_one();
	}

	size_t JobQueue::size() const
	{
		std::unique_lock lock(m_mutexFuncs);
		return m_funcs.size() + m_numRunning;
	}

	void JobQueue::waitEmpty()
	{
		std::unique_lock lock(m_mutexFuncs);

		m_emptyCv.wait(lock, [this] {return m_funcs.empty() && !m_numRunning; });
	}

	size_t JobQueue::pending() const
	{
		std::unique_lock lock(m_mutexFuncs);
		return m_funcs.size();
	}

	void JobQueue::threadFunc()
	{
		while (!m_destroy)
		{
			std::unique_lock lock(m_mutexFuncs);

			m_cv.wait(lock, [this] {return !m_funcs.empty();});

			const auto func = m_funcs.front();

			if (m_destroy)
				return;

			++m_numRunning;
			m_funcs.pop_front();

			lock.unlock();
			func();
			lock.lock();

			--m_numRunning;

			if (m_funcs.empty() && !m_numRunning)
				m_emptyCv.notify_all();
		}
	}

	JobGroup::JobGroup(JobQueue& _queue): m_queue(_queue)
	{
	}

	JobGroup::~JobGroup()
	{
		wait();
	}

	void JobGroup::add(std::function<void()>&& _func)
	{
		{
			std::unique_lock lockCounts(m_mutexCounts);
			++m_countEnqueued;
		}

		auto func = [this, f = std::move(_func)]
		{
			f();
			onFuncCompleted();
		};

		m_queue.add(func);
	}

	void JobGroup::wait()
	{
		std::unique_lock l(m_mutexCounts);
		m_completedCv.wait(l, [this]
			{
				return m_countCompleted == m_countEnqueued;
			}
		);
	}

	void JobGroup::onFuncCompleted()
	{
		std::unique_lock lockCounts(m_mutexCounts);
		++m_countCompleted;

		if (m_countCompleted == m_countEnqueued)
		{
			lockCounts.unlock();
			m_completedCv.notify_one();
		}
	}
}
