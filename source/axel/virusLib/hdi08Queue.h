#pragma once

#include <array>
#include <vector>
#include <deque>
#include <mutex>

#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class HDI08;
	class Memory;
}

namespace virusLib
{
	class Hdi08Queue
	{
	public:
		Hdi08Queue(dsp56k::HDI08& _hdi08, const dsp56k::Memory& _memory);
		Hdi08Queue(Hdi08Queue&& _s) noexcept
		: m_hdi08(_s.m_hdi08)
		, m_memory(_s.m_memory)
		, m_dataRX(std::move(_s.m_dataRX))
		, m_lastHostFlag0(_s.m_lastHostFlag0)
		, m_lastHostFlag1(_s.m_lastHostFlag1)
		, m_nextHostFlags(_s.m_nextHostFlags)
		, m_osRings(_s.m_osRings)
		, m_isTI(_s.m_isTI)
		, m_sentHostFlag0(_s.m_sentHostFlag0)
		{
		}
		Hdi08Queue(const Hdi08Queue&) = delete;

		void writeRX(const std::vector<dsp56k::TWord>& _data);
		void writeRX(const dsp56k::TWord* _data, size_t _count);

		void writeHostFlags(uint8_t _flag0, uint8_t _flag1);

		void exec();

		bool rxEmpty() const;

		dsp56k::HDI08& get() const { return m_hdi08; }

		// call once the OS runs: from then on, a word only goes to the DSP while the OS ring it ends up in has room
		bool enableFlowControl(bool _isTI);

	private:
		struct OsRing
		{
			dsp56k::TWord writePointer = 0;	// x: address, the read pointer is the next word
			dsp56k::TWord size = 0;
		};

		bool rxFull() const;
		bool needsToWaitforHostFlags(uint8_t _flag0, uint8_t _flag1) const;
		void sendPendingData();
		bool osRingHasRoom(dsp56k::TWord _word, uint8_t _hostFlag0) const;

		static constexpr uint8_t HostFlagInvalid = 0xff;

		dsp56k::HDI08& m_hdi08;
		const dsp56k::Memory& m_memory;
		std::deque<dsp56k::TWord> m_dataRX;

		uint8_t m_lastHostFlag0 = HostFlagInvalid;
		uint8_t m_lastHostFlag1 = HostFlagInvalid;

		uint32_t m_nextHostFlags = 0;

		std::array<OsRing, 2> m_osRings{};	// single MIDI bytes, packed words. Size 0 = no flow control
		bool m_isTI = false;
		uint8_t m_sentHostFlag0 = 1;		// host flag 0 of the words sent last

		mutable std::mutex m_mutex;
	};
}
