#include <vector>
#include <chrono>
#include <thread>

#include "syx.h"
#include "midiTypes.h"

using namespace dsp56k;

namespace virusLib
{
Syx::Syx(HDI08& _hdi08, ROMFile& _romFile) : m_hdi08(_hdi08), m_romFile(_romFile)
{
	m_romFile.getMulti(0, m_multiEditBuffer);
}

void Syx::writeHostBitsWithWait(const char flag1, const char flag2) const
{
	const int hsr=m_hdi08.readStatusRegister();
	const int target=(flag1?1:0)|(flag2?2:0);
	if (((hsr>>3)&3)==target) return;
	waitUntilBufferEmpty();
	m_hdi08.setHostFlags(flag1, flag2);
}

void Syx::sendFile(int program, const std::vector<TWord>& preset) const
{
	writeHostBitsWithWait(0,1);
	// Send header
	int buf[] = {0xf47555, 0x100000};
	buf[1] = buf[1] | (program << 8);
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
	send(PAGE_C, 0x0, _command, _value);
}

void Syx::send(const Page _page, const int _part, const int _param, const int _value) const
{
	waitUntilReady();
	writeHostBitsWithWait(0,1);
	int buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | _page;
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);
}

void Syx::sendMIDI(int a,int b,int c) const
{
	writeHostBitsWithWait(1,1);
	int buf[3]={a<<16,b<<16,c<<16};
	m_hdi08.writeRX(buf,3);
}

std::vector<uint8_t> Syx::sendSysex(std::vector<uint8_t> _data) const
{
    uint8_t deviceId = _data[5];
    uint8_t cmd = _data[6];
    std::vector<uint8_t> response;

    LOGFMT("Incoming sysex request [device=0x%x, cmd=0x%x]", deviceId, cmd);
    switch (cmd)
    {
        case DUMP_SINGLE: {
            const uint8_t bank = _data[7];
            const uint8_t program = _data[8];
            std::array<uint8_t, 256> dump;
            std::copy(_data.data() + 9, _data.data() + 9 + 256, dump.begin());
            dumpSingle(bank, program, dump);
            break;
        }
        case REQUEST_MULTI: {
            const uint8_t bank = _data[7];
            const uint8_t program = _data[8];
            std::array<uint8_t, 256> dump;
            const int res = requestMulti(deviceId, bank, program, dump);
            if (res == 0)
            {
                response.resize(550);
                response[0] = M_STARTOFSYSEX;
                response[1] = 0x00;
                response[2] = 0x20;
                response[3] = 0x33;
                response[4] = 0x01;
                response[5] = deviceId;
                response[6] = DUMP_MULTI;  // multi dump
                response[7] = 0x00;        // bank number
                response[8] = 0x00;        // program number

                uint8_t cs = deviceId + 11 + response[7];
                size_t idx = 9;
                for(const uint8_t value : dump)
                {
                    response[idx++] = value;
                    cs += value;
                }
                response[idx++] = cs & 0x7f; // checksum
                response[idx] = M_ENDOFSYSEX;
            }
            break;
        }
        case PARAM_CHANGE_A:
        case PARAM_CHANGE_B:
        case PARAM_CHANGE_C: {
            send((Page)cmd, _data[7], _data[8], _data[9]);
            break;
        }
        default:
            LOG("Unknown sysex cmd " << HEXN((int)cmd, 2));
    }

    return response;
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

int Syx::requestMulti(int _deviceId, int _bank, int _program, std::array<uint8_t, 256>& _data) const
{
	if (_deviceId != m_deviceId) {
		// Ignore messages intended for a different device
		return -1;
	}

	if (_bank == 0)
	{
		// Use multi-edit buffer
		_data = m_multiEditBuffer;
		return 0;
	}

	if (_bank != 1)
		return -1;

	// Load from flash
	m_romFile.getMulti(_program, _data);

	return 0;
}

int Syx::dumpSingle(int _bank, int _program, std::array<uint8_t, 256>& _data) const
{
	if (_bank != 0) {
		// We do not support writing to flash
		return -1;
	}

	// Convert array of uint8_t to vector of 24bit TWord
	int idx = 0;
	std::vector<TWord> preset;
	preset.resize(0x56);
	for (int i = 0; i < 0x56; i++) {
		preset[i] = ((_data[idx] << 16) | (_data[idx + 1] << 8) | _data[idx + 2]);
		if (i == 0x55) {
			// We need to clear the last bytes or it might not load properly!
			preset[i] = preset[i] & 0xff0000;
		}
		idx += 3;
	}
	sendFile(_program, preset);
	return 0;
}

}