#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jetypes.h"

#include "synthLib/midiTypes.h"

namespace jeLib
{
	class Rom
	{
	public:
		using Sysex = synthLib::SysexBuffer;
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

		bool getPresets(std::vector<std::vector<Preset>>& _presets) const;

	private:
		size_t findKey(const char* _key) const;

		void validate();

		Preset getPatch(size_t _index, const Sysex& _presetData) const;
		Preset getPerformance(size_t _index, const Sysex& _presetData) const;

		uint32_t getPresetSize() const;
		uint32_t getPerformanceSize() const;

		std::string m_name;
		std::vector<uint8_t> m_data;
	};
}
