#include "os.h"

#ifdef _MSC_VER
#include <cfloat>
#include <cstdlib>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#elif defined(HAVE_SSE)
#include <immintrin.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <errno.h>
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

    void disableErrorDialogs()
    {
#ifdef _MSC_VER
#ifdef _DEBUG
		// A failed assert writes to stderr and then calls abort(), which reports "abort() has been called"
		// through the debug CRT - as a modal message box unless told otherwise. Nobody clicks it on a build
		// server, so the test hangs until it is killed and the failure looks like a timeout. Report to
		// stderr instead: the message survives in the log and the process dies on its own.
		// _CRT_WARN is left alone, it defaults to the debugger and would only move the debug heap's leak
		// dump into every test log.
		const int reportTypes[] = {_CRT_ERROR, _CRT_ASSERT};

		for (const int reportType : reportTypes)
		{
			_CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
			_CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
		}
#endif
		// Skip the Windows Error Reporting fastfail, so abort() exits with 3 in every configuration.
		// _WRITE_ABORT_MSG stays on: it is the line that says the process aborted rather than returned.
		_set_abort_behavior(0, _CALL_REPORTFAULT);
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
