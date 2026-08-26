#pragma once

#include <vector>
#include <deque>
#include <mutex>

#include "dsp56kEmu/types.h"

namespace dsp56k
{
	class HDI08;
}

namespace virusLib
{
	class Hdi08Queue
	{
	public:
		Hdi08Queue(dsp56k::HDI08& _hdi08);
		Hdi08Queue(Hdi08Queue&& _s) noexcept
		: m_hdi08(_s.m_hdi08)
		, m_dataRX(std::move(_s.m_dataRX))
		, m_lastHostFlag0(_s.m_lastHostFlag0)
		, m_lastHostFlag1(_s.m_lastHostFlag1)
		, m_nextHostFlags(_s.m_nextHostFlags)
		{
		}
		Hdi08Queue(const Hdi08Queue&) = delete;

		void writeRX(const std::vector<dsp56k::TWord>& _data);
		void writeRX(const dsp56k::TWord* _data, size_t _count);

		void writeHostFlags(uint8_t _flag0, uint8_t _flag1);

		void exec();

		bool rxEmpty() const;

		dsp56k::HDI08& get() const { return m_hdi08; }

	private:
		bool rxFull() const;
		bool needsToWaitforHostFlags(uint8_t _flag0, uint8_t _flag1) const;
		void sendPendingData();

		static constexpr uint8_t HostFlagInvalid = 0xff;

		dsp56k::HDI08& m_hdi08;
		std::deque<dsp56k::TWord> m_dataRX;

		uint8_t m_lastHostFlag0 = HostFlagInvalid;
		uint8_t m_lastHostFlag1 = HostFlagInvalid;

		uint32_t m_nextHostFlags = 0;

		mutable std::mutex m_mutex;
	};
}
