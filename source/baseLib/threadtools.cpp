#include "threadtools.h"

#include "logging.h"

#ifdef _WIN32
#	include <Windows.h>
#else
#	include <pthread.h>
#	include <sched.h>
#	include <sys/resource.h>
#	include <string.h>	// strerror
#endif

#ifdef __APPLE__
#	include <mach/mach.h>
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
#elif defined(__APPLE__)
		if (_priority == ThreadPriority::Highest)
		{
			// set some reasonable realtime parameters for audio processing
			setCurrentThreadRealtimeParameters(44100, 2048);
		}
		else if (_priority == ThreadPriority::Low || _priority == ThreadPriority::Lowest)
		{
			pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
		}
#else
		if (_priority == ThreadPriority::Highest)
		{
			// Linux SCHED_RR real-time (requires CAP_SYS_NICE)
	        sched_param sch_params;
	        sch_params.sched_priority = 10; // small RT priority
	        auto result = pthread_setschedparam(pthread_self(), SCHED_RR, &sch_params);
	        if (result != 0)
	        {
				LOG("pthread_setschedparam failed, error " << result << ": " << strerror(result));
	        }
			LOG("Success setting thread to real-time priority");
	        return result == 0;
		}
		if (_priority == ThreadPriority::Low || _priority == ThreadPriority::Lowest)
		{
			setpriority(PRIO_PROCESS, 0, 10);
		}
#endif
		return true;
	}

	bool ThreadTools::setCurrentThreadRealtimeParameters(int _samplerate, int _blocksize)
	{
#ifdef __APPLE__
	    // Compute the nominal "period" between activations, in microseconds.
	    // Example: 44100 Hz, 1024 buffer => 23,219 us
	    double periodUsec = static_cast<double>(_blocksize) * 1'000'000.0 / static_cast<double>(_samplerate);

	    // Reasonable assumptions:
		// computation = 25% - 35% of the period
	    // constraint = equal to or slightly above the period
	    // The exact numbers aren't critical, but they should stay consistent.
	    uint32_t computation = static_cast<uint32_t>(periodUsec * 0.30);
	    uint32_t constraint  = static_cast<uint32_t>(periodUsec * 1.05);
	    uint32_t period      = static_cast<uint32_t>(periodUsec);

	    // Clamp to sane limits
	    computation = std::max<uint32_t>(computation, 1000); // >= 1 ms
	    constraint = std::max<uint32_t>(constraint, computation + 1000); // Always > computation

	    // Prepare Mach real-time policy
	    thread_time_constraint_policy_data_t policy;
	    policy.period       = period;        // expected activation interval
	    policy.computation  = computation;   // estimated CPU time
	    policy.constraint   = constraint;    // must finish before this
	    policy.preemptible  = TRUE;

	    thread_port_t thread = pthread_mach_thread_np(pthread_self());
	    kern_return_t result = thread_policy_set(thread,
	                                             THREAD_TIME_CONSTRAINT_POLICY,
	                                             (thread_policy_t)&policy,
	                                             THREAD_TIME_CONSTRAINT_POLICY_COUNT);

	    if (result == KERN_SUCCESS)
	    {
			LOG("Success setting thread realtime parameters: period=" << period << " us, computation=" << computation << " us, constraint=" << constraint << " us");
	        return false;
	    }
		LOG("Failed to set thread realtime parameters, error code " << result);
#endif
		return false;
	}
}
