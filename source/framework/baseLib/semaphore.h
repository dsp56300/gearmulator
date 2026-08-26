#pragma once

#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace baseLib
{
	class Semaphore
	{
	public:
		explicit Semaphore(const uint32_t _count = 0) : m_count(_count) {}

		void wait()
		{
			std::unique_lock uLock(m_mutex);

			m_cv.wait(uLock, [&]{ return m_count > 0; });

			--m_count;
		}

		void notify()
		{
			{
				std::lock_guard uLockHalt(m_mutex);
				++m_count;
			}
			m_cv.notify_one();
		}

	private:
		std::condition_variable m_cv;
		std::mutex m_mutex;
		uint32_t m_count;
	};
}
