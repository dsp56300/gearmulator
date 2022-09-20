#pragma once

#include <sstream>
#include <string>
#include <iomanip>

namespace mc68k
{
	void logToConsole( const std::string& _s );
}

#define LOG(S)																												\
{																															\
	std::stringstream _ss_logging_cpp;	_ss_logging_cpp << __FUNCTION__ << "@" << __LINE__ << ": " << S;					\
																															\
	mc68k::logToConsole(_ss_logging_cpp.str());																				\
}

#define HEXN(S, n)		std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S
#define HEX(S)			HEXN(S, 8)
