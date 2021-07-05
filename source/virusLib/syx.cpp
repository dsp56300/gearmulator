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

	for(auto i=0; i<static_cast<int>(m_singleEditBuffer.size()); ++i)
		m_romFile.getSingle(0, i, m_singleEditBuffer[i]);
}

bool Syx::needsToWaitForHostBits(char flag1, char flag2) const
{
	const int target = (flag1?1:0)|(flag2?2:0);
	const int hsr = m_hdi08.readStatusRegister();
	if (((hsr>>3)&3)==target) 
		return false;
	return m_hdi08.hasDataToSend();
}

void Syx::sendInitControlCommands()
{
	sendControlCommand(UNK1a, 0x1);
	sendControlCommand(UNK1b, 0x1);
	sendControlCommand(UNK1c, 0x0);
	sendControlCommand(UNK1d, 0x0);
	sendControlCommand(UNK35, 0x40);
	sendControlCommand(UNK36, 0xc);
	sendControlCommand(UNK36, 0xc); // duplicate
	sendControlCommand(SECOND_OUTPUT_SELECT, 0x0);
	sendControlCommand(UNK76, 0x0);
	sendControlCommand(INPUT_THRU_LEVEL, 0x0);
	sendControlCommand(INPUT_BOOST, 0x0);
	sendControlCommand(MASTER_TUNE, 0x40); // issue
	sendControlCommand(DEVICE_ID, 0x0);
	sendControlCommand(MIDI_CONTROL_LOW_PAGE, 0x1);
	sendControlCommand(MIDI_CONTROL_HIGH_PAGE, 0x1);
	sendControlCommand(MIDI_ARPEGGIATOR_SEND, 0x0);
	sendControlCommand(MIDI_CLOCK_RX, 0x1);
	sendControlCommand(GLOBAL_CHANNEL, 0x0);
	sendControlCommand(LED_MODE, 0x2);
	sendControlCommand(LCD_CONTRAST, 0x40);
	sendControlCommand(PANEL_DESTINATION, 0x1);
	sendControlCommand(UNK_6d, 0x6c);
	sendControlCommand(CC_MASTER_VOLUME, 0x7a); // issue
}

void Syx::writeHostBitsWithWait(const char flag1, const char flag2) const
{
	const int hsr=m_hdi08.readStatusRegister();
	const int target=(flag1?1:0)|(flag2?2:0);
	if (((hsr>>3)&3)==target) return;
	waitUntilBufferEmpty();
	m_hdi08.setHostFlags(flag1, flag2);
}

bool Syx::sendFile(int program, const std::vector<TWord>& preset, bool cancelIfFull) const
{
	if(cancelIfFull && needsToWaitForHostBits(0,1))
		return false;

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
	return true;
}

void Syx::sendControlCommand(const ControlCommand _command, const int _value) const
{
	send(PAGE_C, 0x0, _command, _value);
}

bool Syx::send(const Page _page, const int _part, const int _param, const int _value, bool cancelIfFull/* = false*/) const
{
	waitUntilReady();
	if(cancelIfFull && needsToWaitForHostBits(0,1))
		return false;
	writeHostBitsWithWait(0,1);
	int buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | _page;
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);
	return true;
}

bool Syx::sendMIDI(int a,int b,int c, bool cancelIfFull/* = false*/) const
{
	if(cancelIfFull && (needsToWaitForHostBits(1,1) || m_hdi08.dataRXFull()))
		return false;
	writeHostBitsWithWait(1,1);
	int buf[3]={a<<16,b<<16,c<<16};
	m_hdi08.writeRX(buf,3);
	return true;
}

bool Syx::sendSysex(std::vector<uint8_t> _data, bool cancelIfFull, std::vector<uint8_t>& response) const
{
	const auto manufacturerA = _data[1];
	const auto manufacturerB = _data[2];
	const auto manufacturerC = _data[3];
	const auto productId = _data[4];
    const auto deviceId = _data[5];
    const auto cmd = _data[6];

	response.clear();
	
	auto buildPresetResponse = [&](uint8_t _type, uint8_t _bank, uint8_t _program, const TPreset& _dump)
	{
        response.resize(1024);
        response[0] = M_STARTOFSYSEX;
        response[1] = manufacturerA;
        response[2] = manufacturerB;
        response[3] = manufacturerC;
        response[4] = productId;
        response[5] = deviceId;
        response[6] = _type;
        response[7] = _bank;
        response[8] = _program;

        uint8_t cs = deviceId + 11 + response[7];
        size_t idx = 9;
        for(const auto value : _dump)
        {
            response[idx++] = value;
            cs += value;
        }
        response[idx++] = cs & 0x7f; // checksum
        response[idx++] = M_ENDOFSYSEX;
        assert(idx < response.size() && "memory corruption!");
        response.resize(idx);
	};

    LOGFMT("Incoming sysex request [device=0x%x, cmd=0x%x]", deviceId, cmd);
    switch (cmd)
    {
        case DUMP_SINGLE: 
		{
            const uint8_t bank = _data[7];
            const uint8_t program = _data[8];
            TPreset dump;
            std::copy(_data.data() + 9, _data.data() + 9 + 256, dump.begin());
            return sendSingle(bank, program, dump, cancelIfFull);
        }
        case REQUEST_MULTI:
		{
            const uint8_t bank = _data[7];
            const uint8_t program = _data[8];
            TPreset dump;
            const auto res = requestMulti(deviceId, bank, program, dump);
        	if(res)
		        buildPresetResponse(DUMP_MULTI, bank, program, dump);
            break;
        }
		case REQUEST_SINGLE:
        {
            const uint8_t bank = _data[7];
            const uint8_t program = _data[8];
            TPreset dump;
            const auto res = requestSingle(deviceId, bank, program, dump);
        	if(res)
		        buildPresetResponse(DUMP_SINGLE, bank, program, dump);
			break;
        }
        case PARAM_CHANGE_A:
        case PARAM_CHANGE_B:
        case PARAM_CHANGE_C:
    	{
            return send((Page)cmd, _data[7], _data[8], _data[9], cancelIfFull);
        }
        default:
            LOG("Unknown sysex cmd " << HEXN((int)cmd, 2));
    }

    return true;
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

bool Syx::requestMulti(int _deviceId, int _bank, int _program, TPreset& _data) const
{
	if (_deviceId != m_deviceId) {
		// Ignore messages intended for a different device
		return false;
	}

	if (_bank == 0)
	{
		// Use multi-edit buffer
		_data = m_multiEditBuffer;
		return true;
	}

	if (_bank != 1)
		return false;

	// Load from flash
	return m_romFile.getMulti(_program, _data);
}

bool Syx::requestSingle(int _deviceId, int _bank, int _program, TPreset& _data) const
{
	if (_deviceId != m_deviceId) 
	{
		// Ignore messages intended for a different device
		return false;
	}
	
	if (_bank == 0)
	{
		// Use single-edit buffer
		_data = m_singleEditBuffer[_program % m_singleEditBuffer.size()];
		return true;
	}

	// Load from flash
	return m_romFile.getSingle(_bank - 1, _program, _data);
}

bool Syx::sendSingle(int _bank, int _program, TPreset& _data, bool cancelIfFull) const
{
	if (_bank != 0) 
	{
		// We do not support writing to flash
		return false;
	}

	// Convert array of uint8_t to vector of 24bit TWord
	int idx = 0;
	std::vector<TWord> preset;
	preset.resize(0x56);
	for (int i = 0; i < 0x56; i++) {
		if (i == 0x55)
			preset[i] = _data[idx] << 16;
		else
			preset[i] = ((_data[idx] << 16) | (_data[idx + 1] << 8) | _data[idx + 2]);
		idx += 3;
	}
	return sendFile(_program, preset, cancelIfFull);
}

}