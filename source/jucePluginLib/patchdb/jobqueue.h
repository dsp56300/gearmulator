#pragma once

#include <deque>
#include <functional>
#include <mutex>

namespace pluginLib::patchDB
{
	class JobQueue
	{
	public:
		JobQueue(std::string _name = {}, bool _start = true);
		~JobQueue();

		void start();
		void destroy();

		void add(const std::function<void()>& _func);
		bool destroyed() const { return m_destroy; }

	private:
		void threadFunc();

		std::string m_name;
		std::deque<std::function<void()>> m_funcs;
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_destroy = false;
		std::unique_ptr<std::thread> m_thread;
	};
}
