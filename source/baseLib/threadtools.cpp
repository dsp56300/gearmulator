#include "threadtools.h"

#include "logging.h"

#ifdef _WIN32
#	include <Windows.h>
#else
#	include <pthread.h>
#	include <sched.h>
#endif

namespace baseLib
{
#ifdef _WIN32
	constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
#pragma pack(pop)

	void SetThreadName( DWORD dwThreadID, const char* threadName)
	{
		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = threadName;
		info.dwThreadID = dwThreadID;
		info.dwFlags = 0;

		__try  // NOLINT(clang-diagnostic-language-extension-token)
		{
			RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info) );
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}
#endif
	void ThreadTools::setCurrentThreadName(const std::string& _name)
	{
#ifdef _WIN32
		SetThreadName(-1, _name.c_str());
#elif defined(__APPLE__)
		pthread_setname_np(_name.c_str());
#else
		pthread_setname_np(pthread_self(), _name.c_str());
#endif
	}

	bool ThreadTools::setCurrentThreadPriority(ThreadPriority _priority)
	{
#ifdef _WIN32
		int prio;
		switch(_priority)
		{
		case ThreadPriority::Lowest:	prio = THREAD_PRIORITY_LOWEST; break;
		case ThreadPriority::Low:		prio = THREAD_PRIORITY_BELOW_NORMAL; break;
		case ThreadPriority::Normal:	prio = THREAD_PRIORITY_NORMAL; break;
		case ThreadPriority::High:		prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
		case ThreadPriority::Highest:	prio = THREAD_PRIORITY_TIME_CRITICAL; break;
		default: return false;
		}
		if( !::SetThreadPriority(GetCurrentThread(), prio))
		{
			LOG("Failed to set thread priority to " << prio);
			return false;
		}
#else
		const auto max = sched_get_priority_max(SCHED_OTHER);
		const auto min = sched_get_priority_min(SCHED_OTHER);
		const auto normal = (max - min) >> 1;
		const auto above = (max + normal) >> 1;
		const auto below = (min + normal) >> 1;

		int prio;
		switch(_priority)
		{
		case ThreadPriority::Lowest:	prio = min; break;
		case ThreadPriority::Low:		prio = below; break;
		case ThreadPriority::Normal:	prio = normal; break;
		case ThreadPriority::High:		prio = above; break;
		case ThreadPriority::Highest:	prio = max; break;
		default: return false;
		}

		sched_param sch_params;
		sch_params.sched_priority = prio;

		const auto id = pthread_self();

		const auto result = pthread_setschedparam(id, SCHED_OTHER, &sch_params);
		if(result)
		{
			LOG("Failed to set thread priority to " << prio << ", error code " << result);
			return false;
		}
#endif
		return true;
	}
}
