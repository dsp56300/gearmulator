#include "rom.h"

#include "baseLib/filesystem.h"

namespace jeLib
{
	Rom::Rom(const std::string& _filename)
	{
		baseLib::filesystem::readFile(m_data, _filename);
		m_name = baseLib::filesystem::getFilenameWithoutPath(_filename);
	}

	Rom::Rom(const std::vector<uint8_t>& _data, const std::string& _name)
	{
		m_data = _data;
		m_name = _name;
	}

	bool Rom::isValid() const
	{
		return getDeviceType() != DeviceType::Invalid;
	}

	DeviceType Rom::getDeviceType() const
	{
		switch (m_data.size())
		{
		case RomSizeKeyboard:  return DeviceType::Keyboard;
		case RomSizeRack:      return DeviceType::Rack;
		default:               return DeviceType::Invalid;
		}
	}
}
