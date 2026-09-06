#pragma once

#include <functional>
#include <string>
#include <sstream>
#include <iomanip>

namespace networkLib
{
	enum class LogLevel
	{
		Debug,
		Info,
		Warning,
		Error,

		Count
	};

	using LogFunc = std::function<void(LogLevel, const char*, int, const std::string&)>;

	void setLogFunc(const LogFunc& _func);

	void log(LogLevel _level, const char* _func, int _line, const std::string& _message);
}

#define LOGNET(L, S)															\
do																				\
{																				\
	std::stringstream __ss___bridgeLib_logging_h;								\
	__ss___bridgeLib_logging_h << S;											\
																				\
	networkLib::log(L, __func__, __LINE__, __ss___bridgeLib_logging_h.str());	\
}																				\
while(false)

#define HEXN(S, n)		std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S
