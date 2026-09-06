#pragma once

#include "deviceTypes.h"

#include <stdexcept>
#include <string>

namespace synthLib
{
	class DeviceException : public std::runtime_error
	{
	public:
		explicit DeviceException(DeviceError _error, const std::string& _message = std::string());

		DeviceError errorCode() const { return m_errorCode; }

	private:
		const DeviceError m_errorCode;
	};
}
