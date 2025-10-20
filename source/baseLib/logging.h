#pragma once

#include <sstream>
#include <string>
#include <iomanip>

namespace baseLib::logging
{
	typedef void (*LogFunc)(const std::string&);

	void logToConsole( const std::string& _s );
	void logToFile( const std::string& _s );
	void setLogFunc(LogFunc _func);
}

#define LOGTOCONSOLE(ss)	{ baseLib::logging::logToConsole( (ss).str() ); }
#define LOGTOFILE(ss)		{ baseLib::logging::logToFile( (ss).str() ); }

#define LOG(S)																												\
do																															\
{																															\
	std::stringstream __ss__logging_h;	__ss__logging_h << __func__ << "@" << __LINE__ << ": " << S;						\
																															\
	LOGTOCONSOLE(__ss__logging_h)																							\
}																															\
while(false)

#define LOGF(S)																												\
do																															\
{																															\
	std::stringstream __ss__logging_h;	__ss__logging_h << S;																\
																															\
	LOGTOFILE(__ss__logging_h)																								\
}																															\
while (false)

#define HEX(S)			std::hex << std::setfill('0') << std::setw(8) << S
#define HEXN(S, n)		std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S
