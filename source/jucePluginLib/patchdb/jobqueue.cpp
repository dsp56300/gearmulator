#include "jobqueue.h"

#include "dsp56kEmu/threadtools.h"

namespace pluginLib::patchDB
{
	JobQueue::JobQueue(const std::string& _name)
	{
		m_thread.reset(new std::thread([this, _name]
		{
			if(!_name.empty())
				dsp56k::ThreadTools::setCurrentThreadName(_name);
			threadFunc();
		}));
	}

	JobQueue::~JobQueue()
	{
		destroy();
	}

	void JobQueue::destroy()
	{
		if (m_destroy)
			return;

		m_destroy = true;
		add([] {});
		m_thread->join();
		m_thread.reset();
	}

	void JobQueue::add(const std::function<void()>& _func)
	{
		std::unique_lock lock(m_mutex);
		m_funcs.push_back(_func);
		m_cv.notify_one();
	}

	void JobQueue::threadFunc()
	{
		while (!m_destroy)
		{
			std::unique_lock lock(m_mutex);
			m_cv.wait(lock, [this] {return !m_funcs.empty(); });
			const auto func = m_funcs.front();
			m_funcs.pop_front();
			lock.unlock();

			if(!m_destroy)
				func();
		}
	}
}
