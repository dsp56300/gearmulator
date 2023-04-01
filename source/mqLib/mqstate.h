#pragma once

#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>

#include "mqmiditypes.h"

#include "../synthLib/deviceTypes.h"
#include "../synthLib/midiTypes.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace mqLib
{
	class MicroQ;

	using SysEx = std::vector<uint8_t>;
	using Responses = std::vector<SysEx>;

	class State
	{
	public:
		enum class Origin
		{
			Device,
			External
		};

		enum class DumpType
		{
			Single,
			Multi,
			Drum,
			Global,
			Mode,
			SingleQ,

			Count
		};

		struct Dump
		{
			DumpType type;
			SysexCommand cmdRequest;
			SysexCommand cmdDump;
			SysexCommand cmdParamChange;
			SysexCommand cmdParamRequest;
			uint32_t firstParamIndex;
			uint32_t idxParamIndexH;
			uint32_t idxParamIndexL;
			uint32_t idxParamValue;
			uint32_t dumpSize;
		};

		static constexpr Dump g_dumps[] = 
		{
			{DumpType::Single, SysexCommand::SingleRequest, SysexCommand::SingleDump, SysexCommand::SingleParameterChange, SysexCommand::SingleParameterRequest, IdxSingleParamFirst, IdxSingleParamIndexH, IdxSingleParamIndexL, IdxSingleParamValue, 392},
			{DumpType::Multi, SysexCommand::MultiRequest, SysexCommand::MultiDump, SysexCommand::MultiParameterChange, SysexCommand::MultiParameterRequest, IdxMultiParamFirst, IdxMultiParamIndexH, IdxMultiParamIndexL, IdxMultiParamValue, 394},
			{DumpType::Drum, SysexCommand::DrumRequest, SysexCommand::DrumDump, SysexCommand::DrumParameterChange, SysexCommand::DrumParameterRequest, IdxDrumParamFirst, IdxDrumParamIndexH, IdxDrumParamIndexL, IdxDrumParamValue, 393},
			{DumpType::Global, SysexCommand::GlobalRequest, SysexCommand::GlobalDump, SysexCommand::GlobalParameterChange, SysexCommand::GlobalParameterRequest, IdxGlobalParamFirst, IdxGlobalParamIndexH, IdxGlobalParamIndexL, IdxGlobalParamValue, 207},
			{DumpType::Mode, SysexCommand::ModeRequest, SysexCommand::ModeDump , SysexCommand::ModeParameterChange, SysexCommand::ModeParameterRequest, IdxModeParamFirst, IdxModeParamIndexH, IdxModeParamIndexL, IdxModeParamValue, 8},

			{DumpType::SingleQ, SysexCommand::SingleRequest, SysexCommand::SingleDump, SysexCommand::SingleParameterChange, SysexCommand::SingleParameterRequest, IdxSingleParamFirst, IdxSingleParamIndexH, IdxSingleParamIndexL, IdxSingleParamValue, 393},
		};

		using Single = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::Single)].dumpSize>;
		using Multi = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::Multi)].dumpSize>;
		using DrumMap = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::Drum)].dumpSize>;
		using Global = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::Global)].dumpSize>;
		using Mode = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::Mode)].dumpSize>;

		using SingleQ = std::array<uint8_t, g_dumps[static_cast<uint32_t>(DumpType::SingleQ)].dumpSize>;

		State(MicroQ& _mq);

		bool loadState(const SysEx& _sysex);

		bool receive(Responses& _responses, const synthLib::SMidiEvent& _data, Origin _sender);
		bool receive(Responses& _responses, const SysEx& _data, Origin _sender);
		void createInitState();

		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) const;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type);

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

		template<size_t Size> static bool append(SysEx& _dst, const std::array<uint8_t, Size>& _src)
		{
			if(!isValid(_src))
				return false;
			auto src = _src;
			updateChecksum(src);
			_dst.insert(_dst.end(), src.begin(), src.end());
			return true;
		}

		template<size_t Size> static void updateChecksum(std::array<uint8_t, Size>& _src)
		{
			uint8_t& c = _src[_src.size() - 2];
			c = 0;
			for(size_t i=IdxCommand; i<_src.size()-2; ++i)
				c += _src[i];
			c &= 0x7f;
		}

		static bool updateChecksum(SysEx& _src)
		{
			if(_src.size() < 3)
				return false;
			uint8_t& c = _src[_src.size() - 2];
			c = 0;
			for(size_t i=IdxCommand; i<_src.size()-2; ++i)
				c += _src[i];
			c &= 0x7f;
			return true;
		}

		bool parseSingleDump(const SysEx& _data);
		bool parseMultiDump(const SysEx& _data);
		bool parseDrumDump(const SysEx& _data);
		bool parseGlobalDump(const SysEx& _data);
		bool parseModeDump(const SysEx& _data);

		bool modifySingle(const SysEx& _data);
		bool modifyMulti(const SysEx& _data);
		bool modifyDrum(const SysEx& _data);
		bool modifyGlobal(const SysEx& _data);
		bool modifyMode(const SysEx& _data);

		uint8_t* getSingleParameter(const SysEx& _data);
		uint8_t* getMultiParameter(const SysEx& _data);
		uint8_t* getDrumParameter(const SysEx& _data);
		uint8_t* getGlobalParameter(const SysEx& _data);
		uint8_t* getModeParameter(const SysEx& _data);

		bool getSingle(Responses& _responses, const SysEx& _data);
		Single* getSingle(MidiBufferNum _buf, uint8_t _loc);

		bool getMulti(Responses& _responses, const SysEx& _data);
		Multi* getMulti(MidiBufferNum _buf, uint8_t _loc);

		bool getDrumMap(Responses& _responses, const SysEx& _data);
		DrumMap* getDrumMap(MidiBufferNum _buf, uint8_t _loc);

		bool getGlobal(Responses& _responses);
		Global* getGlobal();

		bool getMode(Responses& _responses);
		Mode* getMode();

		bool getDump(DumpType _type, Responses& _responses, const SysEx& _data);
		bool parseDump(DumpType _type, const SysEx& _data);
		bool modifyDump(DumpType _type, const SysEx& _data);
		bool requestDumpParameter(DumpType _type, Responses& _responses, const SysEx& _data);

		uint8_t getGlobalParameter(GlobalParameter _parameter) const;
		void setGlobalParameter(GlobalParameter _parameter, uint8_t _value);

		static bool isValid(const Single& _single)
		{
			return _single[IdxSingleParamFirst] == 1;
		}

		static bool isValid(const Multi& _multi)
		{
			return _multi[IdxMultiParamFirst] > 0;			// Note: no version number in a multi, assume that Multi volume has to be > 0 to be valid
		}

		static bool isValid(const DrumMap& _drum)
		{
			return _drum[IdxDrumParamFirst + 5] >= 4;		// Note: no version number in a drum map, check for transpose value for instrument 0
		}

		static bool isValid(const Global& _global)
		{
			return _global[IdxGlobalParamFirst] == '1';		// yes, this is not an int but an ascii '1' = 49
		}

		static bool isValid(const Mode& _mode)
		{
			return _mode.front() == 0xf0;					// unable to derive from the packet itself
		}

		void forwardToDevice(const SysEx& _data) const;

		void requestGlobal() const;
		void requestSingle(MidiBufferNum _buf, MidiSoundLocation _location) const;
		void requestMulti(MidiBufferNum _buf, uint8_t _location) const;

		MicroQ& m_mq;

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
		Mode m_mode{};

		// current state, valid while receiving data
		Origin m_sender = Origin::External;
		bool m_isEditBuffer = false;

		synthLib::SMidiEvent m_lastBankSelectMSB;
		synthLib::SMidiEvent m_lastBankSelectLSB;
	};
}
