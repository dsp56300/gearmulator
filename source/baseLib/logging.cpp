#include "logging.h"

#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ctime>
#define output_string(s) ::OutputDebugStringA(s)
#else
#define output_string(s) fputs(s, stdout);
#endif

namespace baseLib::logging
{
	void defaultLogToConsole(const std::string& _s)
	{
		output_string( (_s + "\n").c_str() );
	}

	namespace
	{
		LogFunc g_logFunc = &defaultLogToConsole;
	}

	void logToConsole( const std::string& _s)
	{
		g_logFunc(_s);
	}

	std::string buildOutfilename()
	{
		char strTime[128];

		time_t t;
		time(&t);

		tm lt;

		memcpy(&lt,localtime(&t),sizeof(tm));

		strftime( strTime, 127, "%Y-%m-%d-%H-%M-%S", &lt );

		return std::string(strTime) + ".log";
	}

	static const std::string g_outfilename = buildOutfilename();

	std::unique_ptr<std::thread> g_logger;
	std::vector<std::string> g_pendingLogs;
	std::mutex g_logMutex;
	using Guard = std::lock_guard<std::mutex>;
	
	void logToFile( const std::string& _s )
	{
		// enable this to have synchronous logging, in case of a crash where you'll lose the latest logs otherwise
#if 0
		std::ofstream o(g_outfilename, std::ios::app);

		if(o.is_open())
		{
			o << _s << std::endl;
			return;
		}
#endif
//		logToConsole(_s);

		{
			Guard g(g_logMutex);
			g_pendingLogs.push_back(_s);
		}

		if(!g_logger)
		{
			g_logger.reset(new std::thread([]()
			{
				std::ofstream o(g_outfilename, std::ios::app);

				if(o.is_open())
				{
					while(true)
					{
						std::vector<std::string> pendingLogs;
						{							
							Guard g(g_logMutex);
							std::swap(g_pendingLogs, pendingLogs);
						}

						if(pendingLogs.empty())
							std::this_thread::sleep_for(std::chrono::milliseconds(500));

						for(const auto& log : pendingLogs)
							o << log << '\n';

						o.flush();
					}
				}
			}));
		}
	}

	void setLogFunc(const LogFunc _func)
	{
		g_logFunc = _func;
	}
}

