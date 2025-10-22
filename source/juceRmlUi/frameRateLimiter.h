#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace juceRmlUi
{
	class FrameLimiter
	{
	public:
		explicit FrameLimiter(const float _hz = 30.0f) : m_frameDuration(std::chrono::duration<float>(1.0f / _hz))
	    {
	    }

	    void setRate(const float _hz)
		{
			setDelay(1.0f / _hz);
	    }

	    void setDelay(const float _duration)
		{
	        std::lock_guard lock(m_mutex);
			const auto newDuration = std::chrono::duration<float>(_duration);
			if (m_frameDuration == newDuration && _duration > 0)
				return;
	        m_frameDuration = newDuration;
	        m_cv.notify_all();
	    }

	    void wait()
		{
	        std::chrono::duration<float> duration;

	        {
	            std::lock_guard lock(m_mutex);
	            duration = m_frameDuration;
	        }

	        const auto next = m_lastTime + duration;

	        std::unique_lock lock(m_mutex);
	        m_cv.wait_until(lock, next);
	        m_lastTime = std::chrono::steady_clock::now();
	    }

	    void wakeEarly()
		{
			m_cv.notify_all();
	    }

	private:
	    std::chrono::steady_clock::time_point m_lastTime = std::chrono::steady_clock::now();
	    std::chrono::duration<float> m_frameDuration;
	    std::condition_variable m_cv;
	    std::mutex m_mutex;
	};
}
