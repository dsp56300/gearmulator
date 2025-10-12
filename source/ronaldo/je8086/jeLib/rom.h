#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jetypes.h"

namespace jeLib
{
	class Rom
	{
	public:
		using Sysex = std::vector<uint8_t>;
		using Preset = std::vector<Sysex>;

		static constexpr size_t RomSizeKeyboard = 512 * 1024;
		static constexpr size_t RomSizeRack = (1024-128) * 1024;

		Rom() = default;
		explicit Rom(const std::string& _filename);
		explicit Rom(const std::vector<uint8_t>& _data, const std::string& _name);

		const std::vector<uint8_t>& getData() const { return m_data; }
		const std::string& getName() const { return m_name; }

		bool isValid() const;

		DeviceType getDeviceType() const;

		bool getPresets(std::vector<Preset>& _presets) const;

	private:
		static Preset getPatch(size_t _index, const Sysex& _presetData);
		static Preset getPerformance(size_t _size, const Sysex& _presetData);

		std::string m_name;
		std::vector<uint8_t> m_data;
	};
}
