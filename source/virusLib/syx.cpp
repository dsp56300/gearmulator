#include <vector>
#include <chrono>
#include <thread>

#include "syx.h"

Syx::Syx(HDI08& _hdi08) : m_hdi08(_hdi08)
{
}

void Syx::writeHostBitsWithWait(const char flag1, const char flag2) const
{
	const int hsr=m_hdi08.readStatusRegister();
	const int target=(flag1?1:0)|(flag2?2:0);
	if (((hsr>>3)&3)==target) return;
	waitUntilBufferEmpty();
	m_hdi08.setHostFlags(flag1, flag2);
}

void Syx::sendFile(const std::vector<TWord>& preset) const
{
	writeHostBitsWithWait(0,1);
	// Send header
	int buf[] = {0xf47555, 0x104000};
	m_hdi08.writeRX(buf, 2);

	// Send preset
	for (size_t i =0; i < preset.size(); i++)
	{
		auto data = preset[i];
		m_hdi08.writeRX(reinterpret_cast<const int*>(&data), 1);
	}
}

void Syx::sendControlCommand(const ControlCommand _command, const int _value) const
{
	send(PAGE_C, SINGLE, _command, _value);
}

void Syx::send(const Page _page, const int _part, const int _param, const int _value) const
{
	waitUntilReady();
	writeHostBitsWithWait(0,1);
	int buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | (0x70 + _page);
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);
}

void Syx::sendMIDI(int a,int b,int c) const
{
	writeHostBitsWithWait(1,1);
	int buf[3]={a<<16,b<<16,c<<16};
	m_hdi08.writeRX(buf,3);
}

void Syx::waitUntilBufferEmpty() const
{
	while (m_hdi08.hasDataToSend())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::this_thread::yield();
	}
}

void Syx::waitUntilReady() const
{
	while (!bittest(m_hdi08.readControlRegister(), HDI08::HCR_HRIE)) 
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::this_thread::yield();
	}
}
