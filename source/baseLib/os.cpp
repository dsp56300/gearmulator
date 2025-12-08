#include "os.h"

#ifdef _MSC_VER
#include <cfloat>
#elif defined(HAVE_SSE)
#include <immintrin.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace baseLib
{
    void setFlushDenormalsToZero()
    {
#if defined(_MSC_VER)
        _controlfp(_DN_FLUSH, _MCW_DN);
#elif defined(HAVE_SSE)
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
    }

    bool isRunningUnderRosetta()
    {
#ifdef __APPLE__
		int ret = 0;
		size_t size = sizeof(ret);
		if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) == -1) 
		{
			if (errno == ENOENT)
				return false;	// no, native
			return false;		// unable to tell, assume native
		}
		return ret == 1;		// Rosetta if result is 1
#else
		return false;
#endif
   	}
}
