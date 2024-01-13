#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>

#include "dsp56kEmu/threadtools.h"

namespace pluginLib::patchDB
{
	class JobGroup;

	class JobQueue final
	{
	public:
		JobQueue(std::string _name = {}, bool _start = true, const dsp56k::ThreadPriority& _prio = dsp56k::ThreadPriority::Normal, uint32_t _threadCount = 1);
		~JobQueue();

		JobQueue(JobQueue&&) = delete;
		JobQueue(const JobQueue&) = delete;
		JobQueue& operator = (const JobQueue&) = delete;
		JobQueue& operator = (JobQueue&&) = delete;

		void start();
		void destroy();

		void add(std::function<void()>&& _func);
		bool destroyed() const { return m_destroy; }

		size_t size() const;
		bool empty() const { return size() == 0; }
		void waitEmpty();
		size_t pending() const;

	private:
		void threadFunc();

		std::string m_name;
		dsp56k::ThreadPriority m_threadPriority;
		uint32_t m_threadCount;

		std::deque<std::function<void()>> m_funcs;
		mutable std::mutex m_mutexFuncs;
		std::condition_variable m_cv;

		std::condition_variable m_emptyCv;

		bool m_destroy = false;
		uint32_t m_numRunning = 0;

		std::vector<std::unique_ptr<std::thread>> m_threads;
	};

	class JobGroup final
	{
	public:
		explicit JobGroup(JobQueue& _queue);

		JobGroup(JobGroup&&) = delete;
		JobGroup(const JobGroup&) = delete;
		JobGroup& operator = (const JobGroup&) = delete;
		JobGroup& operator = (JobGroup&&) = delete;

		~JobGroup();

		void add(std::function<void()>&&);

		template<typename T>
		void forEach(const T& _container, const std::function<void(const T&)>& _func, bool _wait = true)
		{
			for (const auto& e : _container)
			{
				add([&e, &_func]
				{
					_func(e);
				});
			}

			if (_wait)
				wait();
		}

		void wait();

	private:
		void onFuncCompleted();

		JobQueue& m_queue;

		std::mutex m_mutexCounts;
		uint32_t m_countEnqueued = 0;
		uint32_t m_countCompleted = 0;

		std::condition_variable m_completedCv;
	};
}
