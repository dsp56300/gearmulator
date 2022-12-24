#include "hdi08Queue.h"

#include "dsp56kEmu/hdi08.h"

namespace virusLib
{
	Hdi08Queue::Hdi08Queue(dsp56k::HDI08& _hdi08) : m_hdi08(_hdi08)
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

			if(d & 0x80000000)
			{
				const auto hostFlag0 = static_cast<uint8_t>((d >> 24) & 1);
				const auto hostFlag1 = static_cast<uint8_t>((d >> 25) & 1);

				if(needsToWaitforHostFlags(hostFlag0, hostFlag1))
					break;

				m_hdi08.setHostFlagsWithWait(hostFlag0, hostFlag1);

				d &= 0xffffff;
			}

			m_hdi08.writeRX(&d, 1);

			m_dataRX.pop_front();
		}
	}
}
