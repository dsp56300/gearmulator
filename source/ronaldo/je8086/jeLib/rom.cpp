#include "rom.h"

#include <algorithm>
#include <cstring>

#include "baseLib/filesystem.h"

#include "jemiditypes.h"
#include "state.h"

namespace jeLib
{
	constexpr uint32_t g_presetSizeKeyboard = 0xa0;
	constexpr uint32_t g_presetSizeRack = 0xb0;

	constexpr uint32_t g_performanceSizeKeyboard = 0x180;
	constexpr uint32_t g_performanceSizeRack = 0x1e0;

	constexpr char g_firstPresetName[] = "Spit'n Slide Bs ";
	constexpr char g_firstPerformanceName[] = "Chariots        ";

	Rom::Rom(const std::string& _filename)
	{
		baseLib::filesystem::readFile(m_data, _filename);
		m_name = baseLib::filesystem::getFilenameWithoutPath(_filename);

		validate();
	}

	Rom::Rom(const std::vector<uint8_t>& _data, const std::string& _name)
	{
		m_data = _data;
		m_name = _name;

		validate();
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

	bool Rom::getPresets(std::vector<std::vector<Preset>>& _presets) const
	{
		if (!isValid())
			return false;

		const auto rack = getDeviceType() == DeviceType::Rack;

		// find starting point by searching for the first preset name
		size_t start = findKey(g_firstPresetName);

		if (!start)
			return false;

		// Keyboard:
		// 128 patches in 2 banks
		// 64 performances

		// Rack:
		// 512 patches in 8 banks
		// 256 performances in 4 banks

		uint32_t presetCount = rack ? 512 : 128;
		uint32_t performanceCount = rack ? 256 : 64;

		auto presetSize = rack ? g_presetSizeRack : g_presetSizeKeyboard;
		auto performanceSize = rack ? g_performanceSizeRack : g_performanceSizeKeyboard;

		for (size_t p=0; p<presetCount; ++p)
		{
			auto presetStart = start;
			auto presetEnd = presetStart + presetSize;
			auto presetData = Sysex(m_data.begin() + static_cast<int64_t>(presetStart), m_data.begin() + static_cast<int64_t>(presetEnd));
			auto patch = getPatch(p, presetData);

			if ((p & 63) == 0)
				_presets.emplace_back();

			_presets.back().emplace_back(std::move(patch));

			start += presetSize;
		}

		// for keyboard, performances follow immediately after patches, not for rack
		if (rack)
		{
			start = findKey(g_firstPerformanceName);

			if (!start)
				return false;
		}

		for (size_t p=0; p<performanceCount; ++p)
		{
			auto performanceStart = start;
			auto performanceEnd = performanceStart + performanceSize;
			auto performanceData = Sysex(m_data.begin() + static_cast<int64_t>(performanceStart), m_data.begin() + static_cast<int64_t>(performanceEnd));

			auto performance = getPerformance(p, performanceData);

			if ((p & 63) == 0)
				_presets.emplace_back();

			_presets.back().emplace_back(std::move(performance));

			start += performanceSize;
		}

		return false;
	}

	size_t Rom::findKey(const char* _key) const
	{
		auto keySize = strlen(_key);

		for (size_t i = 0; i < m_data.size() - keySize; ++i)
		{
			if (std::equal(_key, _key + keySize, &m_data[i]))
				return i;
		}
		return 0;
	}

	void Rom::validate()
	{
		if (getDeviceType() == DeviceType::Invalid || !findKey(g_firstPresetName))
		{
			m_data.clear();
			m_name.clear();
		}
	}

	namespace
	{
		template<typename T> Rom::Preset createPreset(const uint32_t _address, const Rom::Sysex& _presetData, const size_t _readOffset = 0, const size_t _maxSize = std::numeric_limits<uint32_t>::max())
		{
			Rom::Preset preset;

			const auto addr4 = State::toAddress(_address);

			Rom::Sysex sysex = State::createHeader(SysexByte::CommandIdDataSet1, SysexByte::DeviceIdDefault, addr4);

			uint32_t paramIndex = 0;

			for (size_t i=_readOffset; i<_presetData.size(); ++i, ++paramIndex)
			{
				if (paramIndex >= _maxSize)
					break;

				size_t paramAddress = (paramIndex & 0x7f) | ((paramIndex & 0x80) << 1);

				const auto param = static_cast<T>(paramAddress);
				auto value = _presetData[i];

				if (State::is14BitData(param))
				{
					sysex.push_back((value & 0x80) >> 7);
					sysex.push_back(value & 0x7f);
					++paramIndex;
				}
				else
				{
					if constexpr(std::is_same_v<T, Patch>)
					{
						if (paramAddress >= static_cast<uint32_t>(Patch::Reserved170))
						{
							// mute assert, for keyboard patches that have been put in the rack, there is crap in here
							value &= 0x7f;
						}

						if (param == Patch::EnvelopeTypeInSolo && value == 0xc0)
						{
							// one broken patch
							value = 0;
						}
					}

					assert(value <= 0x7f);
					sysex.push_back(value & 0x7f);
				}
			}

			State::createFooter(sysex);

			return { sysex };
		}
	}

	Rom::Preset Rom::getPatch(size_t _index, const Sysex& _presetData) const
	{
		if (_presetData.size() != getPresetSize())
			return {};

		uint32_t address = static_cast<uint32_t>(AddressArea::UserPatch);

		if (_index < 64)
		{
			address |= static_cast<uint32_t>(UserPatchArea::UserPatch001);
		}
		else
		{
			address |= static_cast<uint32_t>(UserPatchArea::UserPatch065);
		}

		_index &= 63;

		address += static_cast<uint32_t>(UserPatchArea::BlockSize) * static_cast<uint32_t>(_index);

		const auto maxSize = static_cast<size_t>(getDeviceType() == DeviceType::Rack ? Patch::DataLengthRack : Patch::DataLengthKeyboard);

		return createPreset<Patch>(address, _presetData, 0, maxSize);
	}

	Rom::Preset Rom::getPerformance(size_t _index, const Sysex& _presetData) const
	{
		if (_presetData.size() != getPerformanceSize())
			return {};

		const auto rack = getDeviceType() == DeviceType::Rack;

		if (rack)
		{
			int foo=0;
		}

		// A performance consists of:
		// - PerformanceCommon
		// - Part Upper
		// - Part Lower
		// - Patch Upper
		// - Patch Lower

		uint32_t addressBase = static_cast<uint32_t>(AddressArea::UserPerformance);

		addressBase |= static_cast<uint32_t>(UserPerformanceArea::UserPerformance01);
		addressBase += static_cast<uint32_t>(UserPerformanceArea::BlockSize) * static_cast<uint32_t>(_index & 63);

		const auto performanceCommonSize = static_cast<size_t>(rack ? PerformanceCommon::DataLengthRack : PerformanceCommon::DataLengthKeyboard);

		// we always use the keyboard part size, as the last byte of the rack doesn't seem to be serialized (garbage in there)
		constexpr auto partSize = static_cast<size_t>(Part::DataLengthKeyboard);

		auto performanceCommon = createPreset<PerformanceCommon>(addressBase, _presetData, 0, performanceCommonSize);

		auto off = performanceCommonSize - 3; /* three times 14 bit data which is 8 bit in rom */

		if (rack)
		{
			// TODO is here the voice modulator?
			off += 0x3e;
		}
		else
		{
			// reserved space or something, no useful data in there. What follows next are the two parts
			off += 0x0f;
		}

		auto address = addressBase | static_cast<uint32_t>(PerformanceData::PartUpper);
		auto partH = createPreset<Part>(address, _presetData, off, partSize);

		off += partSize;

		// reserved
		if (rack)
			off += 9;
		else
			off++;

		address = addressBase | static_cast<uint32_t>(PerformanceData::PartLower);
		auto partL = createPreset<Part>(address, _presetData, off, partSize);

		off += partSize;

		// reserved
		if (rack)
			off += 9;
		else
			off++;

		address = addressBase | static_cast<uint32_t>(PerformanceData::PatchUpper);
		auto patchUpper = createPreset<Patch>(address, _presetData, off, static_cast<uint32_t>(rack ? Patch::DataLengthRack : Patch::DataLengthKeyboard));

		off += getPresetSize();

		address = addressBase | static_cast<uint32_t>(PerformanceData::PatchLower);
		auto patchLower = createPreset<Patch>(address, _presetData, off, static_cast<uint32_t>(rack ? Patch::DataLengthRack : Patch::DataLengthKeyboard));

		auto result = std::move(performanceCommon);

		result.insert(result.end(), partH.begin(), partH.end());
		result.insert(result.end(), partL.begin(), partL.end());
		result.insert(result.end(), patchUpper.begin(), patchUpper.end());
		result.insert(result.end(), patchLower.begin(), patchLower.end());

		return result;
	}

	uint32_t Rom::getPresetSize() const
	{
		return getDeviceType() == DeviceType::Rack ? g_presetSizeRack : g_presetSizeKeyboard;
	}

	uint32_t Rom::getPerformanceSize() const
	{
		return getDeviceType() == DeviceType::Rack ? g_performanceSizeRack : g_performanceSizeKeyboard;
	}
}
