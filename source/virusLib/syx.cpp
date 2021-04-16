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

int Syx::sendFile(const char* _path)
{
	std::array<char, 256> values;
	std::ifstream file(_path, std::ios::binary);
	file.seekg(0x9);
	file.read(values.data(), values.size());

	// We need to clear the banks first
	for (int i = 0; i <= 0x7f; i++) {
		send(Syx::PAGE_A, Syx::SINGLE, i, 0);
	}
	for (int i = 0; i <= 0x7f; i++) {
		send(Syx::PAGE_B, Syx::SINGLE, i, 0);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	// Now send the preset
	auto page = Syx::PAGE_A;
	for (int i = 0; i <= 0xff; i++) {
		printf("Sending PAGE=0x%x, KEY=0x%x, VALUE=0x%x\n", page, i % 0x80, values[i]);
		send(page, Syx::SINGLE, i % 0x80, values[i]);
		if (i == 0x7f) {
			page = Syx::PAGE_B;
			i++;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
	int buf[] = {0xf00040, 0x0000f7};
	buf[0] = buf[0] | ((0x70 + _page) << 8) | _part;
	buf[1] = buf[1] | (_param << 16) | (_value << 8);
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
