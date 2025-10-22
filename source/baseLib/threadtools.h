#pragma once

#include <string>

namespace baseLib
{
	enum class ThreadPriority : int8_t
	{
		Lowest = -2,
		Low = -1,
		Normal = 0,
		High = 1,
		Highest = 2
	};

	class ThreadTools
	{
	public:
		static void setCurrentThreadName(const std::string& _name);
		static bool setCurrentThreadPriority(ThreadPriority _priority);
		static bool setCurrentThreadRealtimeParameters(int _samplerate, int _blocksize);
	};
}
