#include "rom.h"

#include <algorithm>

#include "baseLib/filesystem.h"

#include "jemiditypes.h"
#include "state.h"

namespace jeLib
{
	constexpr uint32_t g_presetSize = 0xa0;
	constexpr uint32_t g_performanceSize = 0x180;

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
			auto presetStart = start;
			auto presetEnd = presetStart + g_presetSize;
			auto presetData = Sysex(m_data.begin() + static_cast<int64_t>(presetStart), m_data.begin() + static_cast<int64_t>(presetEnd));
			auto patch = getPatch(p, presetData);

			_presets.push_back(patch);

			start += g_presetSize;
		}

		// performances follow immediately after patches

		for (size_t p=0; p<64; ++p)
		{
			auto performanceStart = start;
			auto performanceEnd = performanceStart + g_performanceSize;
			auto performanceData = Sysex(m_data.begin() + static_cast<int64_t>(performanceStart), m_data.begin() + static_cast<int64_t>(performanceEnd));

			auto performance = getPerformance(p, performanceData);
			_presets.push_back(performance);

			start += g_performanceSize;
		}

		return false;
	}

	namespace
	{
		template<typename T> Rom::Preset createPreset(const uint32_t _address, const Rom::Sysex& _presetData, const size_t _readOffset = 0, const size_t _maxSize = std::numeric_limits<uint32_t>::max())
		{
			Rom::Preset preset;

			const auto addr4 = State::toAddress(_address);

			Rom::Sysex sysex = State::createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr4);

			uint32_t paramIndex = 0;

			for (size_t i=_readOffset; i<std::min(_presetData.size(), _readOffset + _maxSize); ++i, ++paramIndex)
			{
				size_t paramAddress = (paramIndex & 0x7f) | ((paramIndex & 0x80) << 1);

				const auto param = static_cast<T>(paramAddress);
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

	Rom::Preset Rom::getPatch(size_t _index, const Sysex& _presetData)
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

		return createPreset<Patch>(address, _presetData);
	}

	Rom::Preset Rom::getPerformance(size_t _size, const Sysex& _presetData)
	{
		if (_presetData.size() != g_performanceSize)
			return {};

		// A performance consists of:
		// - PerformanceCommon
		// - Part Upper
		// - Part Lower
		// - Patch Upper
		// - Patch Lower

		uint32_t addressBase = static_cast<uint32_t>(AddressArea::UserPerformance);

		addressBase |= static_cast<uint32_t>(UserPerformanceArea::UserPerformance01);
		addressBase += static_cast<uint32_t>(UserPerformanceArea::BlockSize) * static_cast<uint32_t>(_size);

		constexpr auto performanceCommonSize = static_cast<size_t>(PerformanceCommon::DataLengthKeyboard) - 3 /* three times 14 bit data which is 8 bit in rom */;

		auto performanceCommon = createPreset<PerformanceCommon>(addressBase, _presetData, 0, performanceCommonSize);

		auto off = performanceCommonSize;

		// reserved space or something, no useful data in there. What follows next are the two parts
		off += 0x0f;

		auto address = addressBase | static_cast<uint32_t>(PerformanceData::PartUpper);
		auto partH = createPreset<Part>(address, _presetData, off, static_cast<size_t>(Part::DataLengthKeyboard));

		off += static_cast<size_t>(Part::DataLengthKeyboard);

		// reserved byte
		off++;

		address = addressBase | static_cast<uint32_t>(PerformanceData::PartLower);
		auto partL = createPreset<Part>(address, _presetData, off, static_cast<size_t>(Part::DataLengthKeyboard));

		off += static_cast<size_t>(Part::DataLengthKeyboard);

		// reserved byte
		off++;

		address = addressBase | static_cast<uint32_t>(PerformanceData::PatchUpper);
		auto patchUpper = createPreset<Patch>(address, _presetData, off, g_presetSize);

		off += g_presetSize;

		address = addressBase | static_cast<uint32_t>(PerformanceData::PatchLower);
		auto patchLower = createPreset<Patch>(address, _presetData, off, g_presetSize);

		auto result = std::move(performanceCommon);

		result.insert(result.end(), partH.begin(), partH.end());
		result.insert(result.end(), partL.begin(), partL.end());
		result.insert(result.end(), patchUpper.begin(), patchUpper.end());
		result.insert(result.end(), patchLower.begin(), patchLower.end());

		return result;
	}
}
