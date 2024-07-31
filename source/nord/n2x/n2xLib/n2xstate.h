#pragma once

#include <array>
#include <vector>

#include "n2xmiditypes.h"
#include "synthLib/midiTypes.h"

namespace n2x
{
	class Hardware;

	class State
	{
	public:
		enum class ReceiveOrder
		{
			SingleA, SingleB, SingleC, SingleD, Multi, Count
		};

		using SingleDump = std::array<uint8_t, g_singleDumpSize>;
		using MultiDump = std::array<uint8_t, g_multiDumpSize>;

		State(Hardware& _hardware);

		bool getState(std::vector<uint8_t>& _state);
		bool setState(const std::vector<uint8_t>& _state);

		bool receive(const synthLib::SMidiEvent& _ev);

		static void createDefaultSingle(SingleDump& _single, uint8_t _program);
		static void copySingleToMulti(MultiDump& _multi, const SingleDump& _single, uint8_t _index);
		static void createDefaultMulti(MultiDump& _multi);

		template<size_t Size>
		static void createHeader(std::array<uint8_t, Size>& _buffer, uint8_t _msgType, uint8_t _msgSpec);

	private:
		template<size_t Size> bool receive(const std::array<uint8_t, Size>& _data)
		{
			synthLib::SMidiEvent e;
			e.sysex.insert(e.sysex.begin(), _data.begin(), _data.end());
			e.source = synthLib::MidiEventSource::Host;
			return receive(e);
		}
		void send(const synthLib::SMidiEvent& _e) const;

		Hardware& m_hardware;
		std::array<SingleDump, 4> m_singles;
		MultiDump m_multi;
		std::vector<ReceiveOrder> m_receiveOrder;
	};
}
