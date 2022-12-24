#pragma once

#include <array>
#include <vector>

namespace mqLib
{
	enum class GlobalParameter : uint8_t;
	using SysEx = std::vector<uint8_t>;

	using Single = std::array<uint8_t, 392>;
	using Multi = std::array<uint8_t, 394>;
	using Global = std::array<uint8_t, 207>;
	using DrumMap = std::array<uint8_t, 393>;

	class State
	{
		enum class Direction
		{
			ToDevice,
			FromDevice
		};
	public:
		State();

		bool receive(std::vector<SysEx>& _responses, const SysEx& _data, Direction _dir);

	private:
		template<size_t Size>
		bool convertTo(std::array<uint8_t, Size>& _dst, const SysEx& _data)
		{
			if(_data.size() != Size)
				return false;
			std::copy(_data.begin(), _data.end(), _dst.begin());
			return true;
		}

		bool parseSingleDump(const SysEx& _data);
		bool parseMultiDump(const SysEx& _data);
		bool parseDrumDump(const SysEx& _data);
		bool parseGlobalDump(const SysEx& _data);

		uint8_t getGlobalParameter(GlobalParameter _parameter) const;

		std::array<Single, 16> m_multiSingles{Single{}};
		std::array<Single, 4> m_instrumentBuffers{Single{}};
		std::array<Single, 300> m_romSingles{Single{}};
		std::array<DrumMap, 20> m_romDrumMaps{DrumMap{}};
		std::array<Multi, 100> m_romMultis{Multi{}};
		Multi m_multiBuffer{};
		DrumMap m_drumBuffer{};
		Global m_global{};
	};
}
