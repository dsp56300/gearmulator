#include <cassert>
#include <fstream>
#include <array>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "syx.h"

#include "../dsp56300/source/dsp56kEmu/logging.h"


Syx::Syx(HDI08& _hdi08) : m_hdi08(_hdi08)
{
	m_hdi08.setHostFlags(0, 1);
}

int Syx::sendFile(const std::vector<TWord>& preset)
{
	waitUntilBufferEmpty();

	// Send header
	int buf[] = {0xf47555, 0x104000};
	m_hdi08.writeRX(buf, 2);

	// Send preset
	for (int i =0; i < preset.size(); i++)
	{
		waitUntilBufferEmpty();
		TWord data = preset[i];
		m_hdi08.writeRX((const int *)&data, 1);
	}
}

int Syx::sendControlCommand(const Syx::ControlCommand _command, const int _value)
{
	waitUntilReady();
	send(Syx::PAGE_C, Syx::SINGLE, _command, _value);
}

void Syx::send(const Syx::Page _page, const int _part, const int _param, const int _value)
{
	waitUntilBufferEmpty();
	int buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | (0x70 + _page);
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);
}

void Syx::waitUntilBufferEmpty()
{
	while (m_hdi08.hasDataToSend()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::this_thread::yield();
	}
}

void Syx::waitUntilReady()
{
	while (!bittest(m_hdi08.readControlRegister(), HDI08::HCR_HRIE)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::this_thread::yield();
	}
}
