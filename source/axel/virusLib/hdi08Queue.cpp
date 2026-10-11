#include "hdi08Queue.h"

#include "dsp56kBase/logging.h"

#include "dsp56kEmu/hdi08.h"
#include "dsp56kEmu/memory.h"

namespace virusLib
{
	Hdi08Queue::Hdi08Queue(dsp56k::HDI08& _hdi08, const dsp56k::Memory& _memory) : m_hdi08(_hdi08), m_memory(_memory)
	{
	}

	void Hdi08Queue::writeRX(const std::vector<dsp56k::TWord>& _data)
	{
		if(_data.empty())
			return;

		writeRX(&_data.front(), _data.size());
	}

	void Hdi08Queue::writeRX(const dsp56k::TWord* _data, size_t _count)
	{
		if(_count == 0 || !_data)
			return;

		std::lock_guard lock(m_mutex);

		m_dataRX.push_back(_data[0] | m_nextHostFlags);
		m_nextHostFlags = 0;

		for(size_t i=1; i<_count; ++i)
			m_dataRX.push_back(_data[i]);

		sendPendingData();
	}

	void Hdi08Queue::writeHostFlags(uint8_t _flag0, uint8_t _flag1)
	{
		std::lock_guard lock(m_mutex);

		if(m_lastHostFlag0 == _flag0 && m_lastHostFlag1 == _flag1)
			return;

		m_lastHostFlag0 = _flag0;
		m_lastHostFlag1 = _flag1;

		m_nextHostFlags |= static_cast<dsp56k::TWord>(_flag0) << 24;
		m_nextHostFlags |= static_cast<dsp56k::TWord>(_flag1) << 25;
		m_nextHostFlags |= 0x80000000;
	}

	void Hdi08Queue::exec()
	{
		std::lock_guard lock(m_mutex);

		sendPendingData();
	}

	bool Hdi08Queue::rxEmpty() const
	{
		std::lock_guard lock(m_mutex);

		if(!m_dataRX.empty())
			return false;

		if(m_hdi08.hasRXData())
			return false;
		return true;
	}

	bool Hdi08Queue::rxFull() const
	{
		return m_hdi08.dataRXFull();
	}

	bool Hdi08Queue::needsToWaitforHostFlags(uint8_t _flag0, uint8_t _flag1) const
	{
		return m_hdi08.needsToWaitForHostFlags(_flag0, _flag1);
	}

	void Hdi08Queue::sendPendingData()
	{
		while(!m_dataRX.empty() && !rxFull())
		{
			auto d = m_dataRX.front();

			const bool hasHostFlags = (d & 0x80000000) != 0;
			const auto hostFlag0 = hasHostFlags ? static_cast<uint8_t>((d >> 24) & 1) : m_sentHostFlag0;
			const auto hostFlag1 = static_cast<uint8_t>((d >> 25) & 1);

			if(hasHostFlags && needsToWaitforHostFlags(hostFlag0, hostFlag1))
				break;

			if(m_osRings[0].size && !osRingHasRoom(d & 0xffffff, hostFlag0))
				break;

			if(hasHostFlags)
			{
				m_hdi08.setHostFlagsWithWait(hostFlag0, hostFlag1);
				m_sentHostFlag0 = hostFlag0;

				d &= 0xffffff;
			}

			m_hdi08.writeRX(&d, 1);

			m_dataRX.pop_front();
		}
	}

	bool Hdi08Queue::enableFlowControl(const bool _isTI)
	{
		// The OS receive interrupt copies each word into one of two rings, single MIDI bytes or packed words, without
		// checking for room, and the OS main loop drains them once per block. Every Virus OS does it the same way:
		// move #modulo,m0 / movep x:<<M_HORX,x:(r0)+ / move r0,x:writePointer, with the read pointer right behind the
		// write pointer and the ring for single bytes first (EMU-239)
		auto p = [this](const dsp56k::TWord _addr) { return m_memory.get(dsp56k::MemArea_P, _addr); };

		std::array<OsRing, 2> rings{};
		size_t count = 0;

		if(p(0x60) == 0x0bf080)	// jsr isr at the receive vector
		{
			const auto isr = p(0x61);
			dsp56k::TWord modulo = 0;

			for(auto a = isr; a < isr + 48 && count < rings.size(); ++a)
			{
				const auto w = p(a);

				if((w & 0xff00ff) == 0x0500a0)	{ modulo = (w >> 8) & 0xff; continue; }		// move #xx,m0
				if(w == 0x05f420)				{ modulo = p(a + 1); continue; }			// move #>xxxxxx,m0
				if(w != 0x085886)				continue;									// movep x:<<M_HORX,x:(r0)+

				const auto store = p(a + 1);

				if((store & 0xffc0ff) == 0x600000)	rings[count++] = {(store >> 8) & 0x3f, modulo + 1};	// move r0,x:aa
				else if(store == 0x607000)			rings[count++] = {p(a + 2), modulo + 1};				// move r0,x:>aaaaaa
				else								break;
			}
		}

		if(count != rings.size())
		{
			LOG("HDI08 flow control off, the receive interrupt of this OS is unknown");
			return false;
		}

		LOG("HDI08 flow control on, OS rings of " << rings[0].size << " and " << rings[1].size << " words");

		std::lock_guard lock(m_mutex);
		m_osRings = rings;
		m_isTI = _isTI;
		return true;
	}

	bool Hdi08Queue::osRingHasRoom(const dsp56k::TWord _word, const uint8_t _hostFlag0) const
	{
		// TI: bit 7 of the low byte selects the ring for single bytes, A/B/C: host flag 0
		const auto& ring = m_osRings[(m_isTI ? (_word & 0x80) != 0 : _hostFlag0 != 0) ? 0 : 1];

		// The words the OS did not take yet count as well. Read before the ring, a word that moves on in between then
		// counts twice instead of not at all
		const auto pending = static_cast<dsp56k::TWord>(m_hdi08.rxData().size());

		const auto write = m_memory.get(dsp56k::MemArea_X, ring.writePointer);
		const auto read = m_memory.get(dsp56k::MemArea_X, ring.writePointer + 1);

		return ((write - read) & (ring.size - 1)) + pending + 1 < ring.size;
	}
}
