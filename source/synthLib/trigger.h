#pragma once

#include <mutex>
#include <condition_variable>
#include <cstdint>

namespace synthLib
{
	template<typename TCounter = uint32_t>
	class Trigger
	{
	public:
		void wait()
		{
			std::unique_lock uLock(m_haltMutex);

			m_haltcv.wait(uLock, [&]
			{
				return m_notifyCounter > m_waitCounter;
			});

			++m_waitCounter;
		}

		void notify()
		{
			{
				std::lock_guard uLockHalt(m_haltMutex);
				++m_notifyCounter;
			}
			m_haltcv.notify_one();
		}

	private:
		std::condition_variable m_haltcv;
		std::mutex m_haltMutex;
		TCounter m_waitCounter = 0;
		TCounter m_notifyCounter = 0;
	};
}
