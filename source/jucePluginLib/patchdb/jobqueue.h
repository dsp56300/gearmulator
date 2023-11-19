#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>

#include "dsp56kEmu/threadtools.h"

namespace pluginLib::patchDB
{
	class JobQueue
	{
	public:
		JobQueue(std::string _name = {}, bool _start = true, const dsp56k::ThreadPriority& _prio = dsp56k::ThreadPriority::Normal, uint32_t _threadCount = 1);
		~JobQueue();

		void start();
		void destroy();

		void add(const std::function<void()>& _func);
		void add(std::function<void()>&& _func);
		bool destroyed() const { return m_destroy; }

		size_t size() const;
		bool empty() const { return size() == 0; }
		void waitEmpty();

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
}
