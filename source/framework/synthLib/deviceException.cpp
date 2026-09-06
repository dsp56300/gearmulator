#include "deviceException.h"

#include <string>

namespace synthLib
{
	static std::string createDefaultErrorMessage(const DeviceError _error)
	{
		switch (_error)
		{
		case DeviceError::None: 			return "No Error, code " + std::to_string(static_cast<int32_t>(_error));
		case DeviceError::Unknown: 			return "Unknown Error, code " + std::to_string(static_cast<int32_t>(_error));
		case DeviceError::FirmwareMissing:	return "The firmware file for this device is missing.";
		default:;							return "Error code " + std::to_string(static_cast<int32_t>(_error));
		}
	}

	DeviceException::DeviceException(DeviceError _error, const std::string& _message) : std::runtime_error(_message.empty() ? createDefaultErrorMessage(_error) : _message), m_errorCode(_error)
	{
	}
}
