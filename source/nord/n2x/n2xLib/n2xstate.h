#pragma once

#include <array>
#include <vector>
#include <cstddef>
#include <unordered_map>
#include <string>

#include "n2xmiditypes.h"
#include "n2xtypes.h"

#include "synthLib/midiTypes.h"

namespace n2x
{
	class Hardware;

	class State
	{
	public:
		using SingleDump = std::array<uint8_t, g_singleDumpWithNameSize>;
		using MultiDump = std::array<uint8_t, g_multiDumpWithNameSize>;

		explicit State(Hardware* _hardware);

		bool getState(std::vector<uint8_t>& _state);
		bool setState(const std::vector<uint8_t>& _state);

		bool receive(const synthLib::SMidiEvent& _ev)
		{
			std::vector<synthLib::SMidiEvent> responses;
			return receive(responses, _ev);
		}
		bool receive(std::vector<synthLib::SMidiEvent>& _responses, const synthLib::SMidiEvent& _ev);
		bool receive(const std::vector<uint8_t>& _data, synthLib::MidiEventSource _source);

		bool receiveNonSysex(const synthLib::SMidiEvent& _ev);

		bool changeSingleParameter(uint8_t _part, SingleParam _parameter, uint8_t _value);
		bool changeMultiParameter(MultiParam _parameter, uint8_t _value);

		static bool changeSingleParameter(SingleDump& _dump, SingleParam _param, uint8_t _value);

		template<typename TDump>
		static bool changeDumpParameter(TDump& _dump, uint32_t _offset, uint8_t _value)
		{
			const auto current = unpackNibbles(_dump, _offset);
			if(current == _value)
				return false;
			packNibbles(_dump, _offset, _value);
			return true;
		}

		void updateMultiFromSingles();

		const auto& getMulti() const { return m_multi; }
		const auto& getSingle(uint8_t _part) const { return m_singles[_part]; }

		const auto& updateAndGetMulti()
		{
			updateMultiFromSingles();
			return getMulti();
		}

		static void createDefaultSingle(SingleDump& _single, uint8_t _program, uint8_t _bank = n2x::SingleDumpBankEditBuffer);
		static void copySingleToMulti(MultiDump& _multi, const SingleDump& _single, uint8_t _index);
		static void extractSingleFromMulti(SingleDump& _single, const MultiDump& _multi, uint8_t _index);
		static void createDefaultMulti(MultiDump& _multi, uint8_t _bank = SysexByte::MultiDumpBankEditBuffer);

		template<size_t Size>
		static void createHeader(std::array<uint8_t, Size>& _buffer, uint8_t _msgType, uint8_t _msgSpec);

		static uint32_t getOffsetInSingleDump(SingleParam _param);
		static uint32_t getOffsetInMultiDump(MultiParam _param);

		uint8_t getPartMidiChannel(const uint8_t _part) const
		{
			return getPartMidiChannel(m_multi, _part);
		}

		std::vector<uint8_t> getPartsForMidiChannel(const synthLib::SMidiEvent& _ev) const;
		std::vector<uint8_t> getPartsForMidiChannel(uint8_t _channel) const;

		template<typename TDump> static uint8_t getPartMidiChannel(const TDump& _dump, const uint8_t _part)
		{
			return getMultiParam(_dump, static_cast<MultiParam>(SlotAMidiChannel + _part), 0);
		}

		uint8_t getMultiParam(const MultiParam _param, const uint8_t _part) const
		{
			return getMultiParam(m_multi, _param, _part);
		}

		template<typename TDump> static uint8_t getMultiParam(const TDump& _dump, const MultiParam _param, const uint8_t _part)
		{
			const auto off = getOffsetInMultiDump(_param) + (_part << 2);

			return unpackNibbles<TDump>(_dump, off);
		}

		template<typename TDump> static uint8_t getSingleParam(const TDump& _dump, const SingleParam _param, const uint8_t _part)
		{
			const auto off = getOffsetInSingleDump(_param) + (_part << 2);

			return unpackNibbles<TDump>(_dump, off);
		}

		template<typename TDump> static uint8_t unpackNibbles(const TDump& _dump, uint32_t _off)
		{
			return static_cast<uint8_t>((_dump[_off] & 0xf) | (_dump[_off + 1] << 4));
		}

		template<typename TDump> static void packNibbles(TDump& _dump, uint32_t _off, const uint8_t _value)
		{
			_dump[_off  ] = _value & 0x0f;
			_dump[_off+1] = _value >> 4;
		}

		static std::vector<uint8_t> createKnobSysex(KnobType _type, uint8_t _value);
		static bool parseKnobSysex(KnobType& _type, uint8_t& _value, const std::vector<uint8_t>& _sysex);

		bool getKnobState(uint8_t& _result, KnobType _type) const;

		static bool isSingleDump(const std::vector<uint8_t>& _dump);
		static bool isMultiDump(const std::vector<uint8_t>& _dump);

		static std::string extractPatchName(const std::vector<uint8_t>& _dump);
		static bool isDumpWithPatchName(const std::vector<uint8_t>& _dump);
		static std::vector<uint8_t> stripPatchName(const std::vector<uint8_t>& _dump);
		static bool isValidPatchName(const std::vector<uint8_t>& _dump);
		static std::vector<uint8_t> validateDump(const std::vector<uint8_t>& _dump);

	private:
		template<size_t Size> bool receive(const std::array<uint8_t, Size>& _data)
		{
			synthLib::SMidiEvent e;
			e.sysex.insert(e.sysex.begin(), _data.begin(), _data.end());
			e.source = synthLib::MidiEventSource::Host;
			return receive(e);
		}

		void send(const synthLib::SMidiEvent& _e) const;

		Hardware* m_hardware;
		std::array<SingleDump, 4> m_singles;
		MultiDump m_multi;
		std::unordered_map<KnobType, uint8_t> m_knobStates;
	};
}
