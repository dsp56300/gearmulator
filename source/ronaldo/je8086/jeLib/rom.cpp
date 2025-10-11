#include "rom.h"

#include <algorithm>

#include "baseLib/filesystem.h"

#include "jemiditypes.h"
#include "state.h"

namespace jeLib
{
	constexpr uint32_t g_presetSize = 160;

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

	bool Rom::getPresets(std::vector<Preset>& _presets) const
	{
		if (!isValid())
			return false;

		// find starting point by searching for the first preset name
		constexpr char key[] = "Spit'n Slide Bs ";

		constexpr size_t nameSize = std::size(key) - 1;

		size_t start = 0;

		for (size_t i = 0; i < m_data.size() - nameSize; ++i)
		{
			if (std::equal(std::begin(key), std::end(key) - 1, &m_data[i]))
			{
				start = i;
				break;
			}
		}

		if (!start)
			return false;

		// There are:
		// 128 patches, each of them having 160 bytes
		// 64 performances

		for (size_t p=0; p<128; ++p)
		{
			auto presetStart = start + p * 160;
			auto presetEnd = presetStart + 160;
			auto presetData = Sysex(m_data.begin() + static_cast<int64_t>(presetStart), m_data.begin() + static_cast<int64_t>(presetEnd));
			auto patch = getPatch(p, presetData);

			_presets.push_back(patch);
		}

		return false;
	}

	Rom::Preset Rom::getPatch(size_t _index, const Sysex& _presetData) const
	{
		if (_presetData.size() != g_presetSize)
			return {};

		uint32_t address = static_cast<uint32_t>(AddressArea::UserPatch);

		if (_index < 64)
		{
			address |= static_cast<uint32_t>(UserPatchArea::UserPatch001);
		}
		else
		{
			address |= static_cast<uint32_t>(UserPatchArea::UserPatch065);
			_index -= 64;
		}

		address += static_cast<uint32_t>(UserPatchArea::BlockSize) * static_cast<uint32_t>(_index);

		const auto addr4 = State::toAddress(address);

		Sysex sysex = State::createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr4);

		uint32_t paramIndex = 0;

		for (size_t i=0; i<_presetData.size(); ++i, ++paramIndex)
		{
			size_t paramAddress = (paramIndex & 0x7f) | ((paramIndex & 0x80) << 1);

			const auto param = static_cast<Patch>(paramAddress);
			const auto value = _presetData[i];
			if (State::is14BitData(param))
			{
				sysex.push_back((value & 0x80) >> 7);
				sysex.push_back(value & 0x7f);
				++paramIndex;
			}
			else
			{
				assert(value <= 0x7f);
				sysex.push_back(value & 0x7f);
			}
		}

		State::createFooter(sysex);

		return { sysex };
	}
}
