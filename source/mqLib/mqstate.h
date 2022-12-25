#pragma once

#include <array>
#include <vector>

namespace mqLib
{
	enum class MidiBufferNum : uint8_t;
	enum class GlobalParameter : uint8_t;

	using SysEx = std::vector<uint8_t>;
	using Responses = std::vector<SysEx>;

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

		bool receive(Responses& _responses, const SysEx& _data, Direction _dir);

	private:
		template<size_t Size> static bool convertTo(std::array<uint8_t, Size>& _dst, const SysEx& _data)
		{
			if(_data.size() != Size)
				return false;
			std::copy(_data.begin(), _data.end(), _dst.begin());
			return true;
		}

		template<size_t Size> static SysEx convertTo(const std::array<uint8_t, Size>& _src)
		{
			SysEx dst;
			dst.insert(dst.begin(), _src.begin(), _src.end());
			return dst;
		}

		bool parseSingleDump(const SysEx& _data);
		bool parseMultiDump(const SysEx& _data);
		bool parseDrumDump(const SysEx& _data);
		bool parseGlobalDump(const SysEx& _data);

		bool modifySingle(const SysEx& _data);
		bool modifyMulti(const SysEx& _data);
		bool modifyDrum(const SysEx& _data);
		bool modifyGlobal(const SysEx& _data);

		uint8_t* getSingleParameter(const SysEx& _data);
		uint8_t* getMultiParameter(const SysEx& _data);
		uint8_t* getDrumParameter(const SysEx& _data);
		uint8_t* getGlobalParameter(const SysEx& _data);

		bool getSingle(Responses& _responses, const SysEx& _data);
		Single* getSingle(MidiBufferNum _buf, uint8_t _loc);

		bool getMulti(Responses& _responses, const SysEx& _data);
		Multi* getMulti(MidiBufferNum _buf, uint8_t _loc);

		bool getDrumMap(Responses& _responses, const SysEx& _data);
		DrumMap* getDrumMap(MidiBufferNum _buf, uint8_t _loc);

		bool getGlobal(Responses& _responses, const SysEx& _data) const;
		Global& getGlobal();

		uint8_t getGlobalParameter(GlobalParameter _parameter) const;

		// ROM
		std::array<Single, 300> m_romSingles{Single{}};
		std::array<DrumMap, 20> m_romDrumMaps{DrumMap{}};
		std::array<Multi, 100> m_romMultis{Multi{}};

		// Edit Buffers
		std::array<Single, 16> m_currentMultiSingles{Single{}};
		std::array<Single, 4> m_currentInstrumentSingles{Single{}};
		std::array<Single, 32> m_currentDrumMapSingles{Single{}};
		Multi m_currentMulti{};
		DrumMap m_currentDrumMap{};

		// Global settings, only available once
		Global m_global{};
	};
}
