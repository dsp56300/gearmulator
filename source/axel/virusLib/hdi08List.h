#pragma once

#include <vector>

#include "hdi08Queue.h"

namespace virusLib
{
	class Hdi08List
	{
	public:
		Hdi08List()
		{
			m_queues.reserve(2);
		}

		void addHDI08(dsp56k::HDI08& _hdi08);
		bool rxEmpty() const;
		void exec();
		size_t size() const { return m_queues.size(); }

		dsp56k::HDI08& getHDI08(size_t _index) { return getQueue(_index).get(); }
		Hdi08Queue& getQueue(size_t _index) { return m_queues[_index]; }

		void writeRX(const dsp56k::TWord* _buf, size_t _length);

		void writeRX(const std::vector<dsp56k::TWord>& _data)
		{
			writeRX(&_data[0], _data.size());
		}

		void writeHostFlags(uint8_t _flag0, uint8_t _flag1);

	private:
		std::vector<Hdi08Queue> m_queues;
	};
}
