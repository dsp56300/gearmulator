#include "logging.h"

#include <iostream>

#include <cstdint>

namespace networkLib
{
	constexpr const char* g_logLevelNames[] =
	{
		"Debug",
		"INFO",
		"WARNING",
		"ERROR"
	};

	static_assert(std::size(g_logLevelNames) == static_cast<uint32_t>(LogLevel::Count));

	void logStdOut(LogLevel _level, const char* _func, int _line, const std::string& _message)
	{
		std::cout << g_logLevelNames[static_cast<uint32_t>(_level)] << ": " << _func << '@' << _line << ": " << _message << '\n';
	}

	namespace
	{
		LogFunc g_logFunc = logStdOut;
	}

	void setLogFunc(const LogFunc& _func)
	{
		if(!_func)
			g_logFunc = logStdOut;
		else
			g_logFunc = _func;
	}

	void log(LogLevel _level, const char* _func, int _line, const std::string& _message)
	{
		g_logFunc(_level, _func, _line, _message);
	}
}
