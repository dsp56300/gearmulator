#pragma once

#include <stdexcept>
#include <string>

namespace networkLib
{
	enum ExceptionType
	{
		ConnectionClosed,
		ConnectionLost
	};

	class NetException : public std::runtime_error
	{
	public:
		NetException(const ExceptionType _type, const std::string& _err) : std::runtime_error(_err), m_type(_type)
		{
		}

		auto type() const { return m_type; }

	private:
		ExceptionType m_type;
	};
}
