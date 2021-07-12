#include <vector>
#include <chrono>
#include <thread>

#include "microcontroller.h"

#include "../synthLib/midiTypes.h"

using namespace dsp56k;
using namespace synthLib;

constexpr uint32_t g_sysexPresetHeaderSize = 9;

namespace virusLib
{
Microcontroller::Microcontroller(HDI08& _hdi08, ROMFile& _romFile) : m_hdi08(_hdi08), m_rom(_romFile), m_currentBank({0})
{
	m_currentBank.fill(0);
	m_rom.getMulti(0, m_multiEditBuffer);

	for(auto i=0; i<static_cast<int>(m_singleEditBuffer.size()); ++i)
		m_rom.getSingle(0, i, m_singleEditBuffer[i]);
}

void Microcontroller::sendInitControlCommands()
{
	sendControlCommand(MIDI_CLOCK_RX, 0x1);				// Enable MIDI clock receive
	sendControlCommand(GLOBAL_CHANNEL, 0x0);			// Set global midi channel to 0
	sendControlCommand(MIDI_CONTROL_LOW_PAGE, 0x1);		// Enable midi CC to edit parameters on page A
	sendControlCommand(MIDI_CONTROL_HIGH_PAGE, 0x1);	// Enable poly pressure to edit parameters on page B
	sendControlCommand(CC_MASTER_VOLUME, 100);			// Set master volume to 100
}

void Microcontroller::createDefaultState()
{
//	m_syx.send(Syx::PAGE_C, 0, Syx::PLAY_MODE, 2); // enable multi mode

	TPreset preset;
	
	// Send preset
//	m_rom.loadPreset(0, 93, preset);	// RepeaterJS
//	m_rom.loadPreset(0, 6, preset);		// BusysawsSV
//	m_rom.loadPreset(0, 12, preset);	// CommerseSV on Virus C
//	m_rom.loadPreset(0, 268, preset);	// CommerseSV on Virus B
//	m_rom.getSingle(0, 116, preset);	// Virus B: Choir 4 BC
	m_rom.getSingle(0, 1, preset);

	sendSingle(0, 0, preset, false);
}

bool Microcontroller::needsToWaitForHostBits(char flag1, char flag2) const
{
	const int target = (flag1?1:0)|(flag2?2:0);
	const int hsr = m_hdi08.readStatusRegister();
	if (((hsr>>3)&3)==target) 
		return false;
	return m_hdi08.hasDataToSend();
}

void Microcontroller::writeHostBitsWithWait(const char flag1, const char flag2) const
{
	const int hsr=m_hdi08.readStatusRegister();
	const int target=(flag1?1:0)|(flag2?2:0);
	if (((hsr>>3)&3)==target) return;
	waitUntilBufferEmpty();
	m_hdi08.setHostFlags(flag1, flag2);
}

bool Microcontroller::sendPreset(uint32_t program, const std::vector<TWord>& preset, bool cancelIfFull, bool isMulti) const
{
	if(cancelIfFull && needsToWaitForHostBits(0,1))
		return false;

	writeHostBitsWithWait(0,1);
	// Send header
	int buf[] = {0xf47555, isMulti ? 0x110000 : 0x100000};
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

void Microcontroller::sendControlCommand(const ControlCommand _command, const int _value) const
{
	send(PAGE_C, 0x0, _command, _value);
}

bool Microcontroller::send(const Page _page, const int _part, const int _param, const int _value, bool cancelIfFull/* = false*/) const
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

bool Microcontroller::sendMIDI(int a,int b,int c, bool cancelIfFull/* = false*/)
{
	const auto channel = a & 0x0f;
	const auto status = a & 0xf0;

	switch (status)
	{
	case M_PROGRAMCHANGE:
		{
			TPreset single;
			if(m_rom.getSingle(m_currentBank[channel], b, single))
			{
				return sendSingle(0, channel, single, cancelIfFull);
			}
		}
	case M_CONTROLCHANGE:
		switch(b)
		{
		case MC_BANKSELECTLSB:
		case MC_BANKSELECTMSB:
			m_currentBank[channel] = c & 7;	 // TODO: max bank count? 8 on C but 6 on B
			return true;
		}
		break;
	}

	if(cancelIfFull && (needsToWaitForHostBits(1,1) || m_hdi08.dataRXFull()))
		return false;
	writeHostBitsWithWait(1,1);
	int buf[3]={a<<16,b<<16,c<<16};
	m_hdi08.writeRX(buf,3);
	return true;
}

bool Microcontroller::sendSysex(const std::vector<uint8_t>& _data, bool _cancelIfFull, std::vector<uint8_t>& _response)
{
	const auto manufacturerA = _data[1];
	const auto manufacturerB = _data[2];
	const auto manufacturerC = _data[3];
	const auto productId = _data[4];
    const auto deviceId = _data[5];
    const auto cmd = _data[6];

	_response.clear();

	if (deviceId != m_deviceId) 
	{
		// ignore messages intended for a different device
		return true;
	}

	auto buildPresetResponse = [&](uint8_t _type, uint8_t _bank, uint8_t _program, const TPreset& _dump)
	{
        _response.resize(1024);
        _response[0] = M_STARTOFSYSEX;
        _response[1] = manufacturerA;
        _response[2] = manufacturerB;
        _response[3] = manufacturerC;
        _response[4] = productId;
        _response[5] = deviceId;
        _response[6] = _type;
        _response[7] = _bank;
        _response[8] = _program;

        uint8_t cs = deviceId + 11 + _response[7];
        size_t idx = 9;
        for(const auto value : _dump)
        {
            _response[idx++] = value;
            cs += value;
        }
        _response[idx++] = cs & 0x7f; // checksum
        _response[idx++] = M_ENDOFSYSEX;
        assert(idx < response.size() && "memory corruption!");
        _response.resize(idx);
	};

    switch (cmd)
    {
        case DUMP_SINGLE: 
			{
	            const uint8_t bank = _data[7];
	            const uint8_t program = _data[8];
			    LOG("Dump Single, Bank " << bank << ", program " << program);
	            TPreset dump;
	            std::copy_n(_data.data() + g_sysexPresetHeaderSize, dump.size(), dump.begin());
	            return sendSingle(bank, program, dump, _cancelIfFull);
	        }
		case DUMP_MULTI: 
			{
	            const uint8_t bank = _data[7];
	            const uint8_t program = _data[8];
			    LOG("Dump Multi, Bank " << bank << ", program " << program);
	            TPreset dump;
	            std::copy_n(_data.data() + g_sysexPresetHeaderSize, dump.size(), dump.begin());
	            return sendMulti(bank, program, dump, _cancelIfFull);
	        }
		case REQUEST_SINGLE:
	        {
	            const uint8_t bank = _data[7];
	            const uint8_t program = _data[8];
			    LOG("Request Single, Bank " << bank << ", program " << program);
	            TPreset dump;
	            const auto res = requestSingle(bank, program, dump);
				if(res)
					buildPresetResponse(DUMP_SINGLE, bank, program, dump);
				break;
	        }
        case REQUEST_MULTI:
			{
	            const uint8_t bank = _data[7];
	            const uint8_t program = _data[8];
			    LOG("Request Multi, Bank " << bank << ", program " << program);
	            TPreset dump;
	            const auto res = requestMulti(bank, program, dump);
        		if(res)
			        buildPresetResponse(DUMP_MULTI, bank, program, dump);
	            break;
	        }
        case PARAM_CHANGE_A:
        case PARAM_CHANGE_B:
        case PARAM_CHANGE_C:
    		{
        		const auto page = static_cast<Page>(cmd);

        		if(page == PAGE_C && _data[8] == PLAY_MODE)
       			{
       				switch(_data[9])
       				{
					case 0:	// Single
						{
			                TPreset single;
			                if(!m_rom.getSingle(0, 0, single))
				                return true;       				

							LOG("Switch to Single mode");
			                return sendSingle(0, SINGLE, single, _cancelIfFull);
						}
					case 1:
						// TODO: Single-Multi
						break;
					default:
						{
							TPreset multi;
       						if(!m_rom.getMulti(0, multi))
								return true;

							LOG("Switch to Multi mode");
							return sendMulti(0, 0, multi, _cancelIfFull);
						}
       				}
	            }

				return send(page, _data[7], _data[8], _data[9], _cancelIfFull);
	        }
        default:
            LOG("Unknown sysex command " << HEXN(cmd, 2));
    }

    return true;
}

void Microcontroller::waitUntilBufferEmpty() const
{
	while (m_hdi08.hasDataToSend())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		std::this_thread::yield();
	}
}

std::vector<TWord> Microcontroller::presetToDSPWords(const TPreset& _preset)
{
	int idx = 0;

	std::vector<TWord> preset;
	preset.resize(0x56);

	for (int i = 0; i < 0x56; i++)
	{
		if (i == 0x55)
			preset[i] = _preset[idx] << 16;
		else
			preset[i] = ((_preset[idx] << 16) | (_preset[idx + 1] << 8) | _preset[idx + 2]);
		idx += 3;
	}

	return preset;
}

void Microcontroller::waitUntilReady() const
{
	while (!bittest(m_hdi08.readControlRegister(), HDI08::HCR_HRIE)) 
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::this_thread::yield();
	}
}

bool Microcontroller::requestMulti(int _bank, int _program, TPreset& _data) const
{
	if (_bank == 0)
	{
		// Use multi-edit buffer
		_data = m_multiEditBuffer;
		return true;
	}

	if (_bank != 1)
		return false;

	// Load from flash
	return m_rom.getMulti(_program, _data);
}

bool Microcontroller::requestSingle(int _bank, int _program, TPreset& _data) const
{
	if (_bank == 0)
	{
		// Use single-edit buffer
		_data = m_singleEditBuffer[_program % m_singleEditBuffer.size()];
		return true;
	}

	// Load from flash
	return m_rom.getSingle(_bank - 1, _program, _data);
}

bool Microcontroller::sendSingle(int _bank, int _program, TPreset& _data, bool cancelIfFull)
{
	if (_bank != 0) 
	{
		LOG("We do not support writing to flash, attempt to write single to bank " << _bank << ", program " << _program);
		return true;
	}

	m_singleEditBuffer[_program % m_singleEditBuffer.size()] = _data;

	// Convert array of uint8_t to vector of 24bit TWord
	return sendPreset(_program, presetToDSPWords(_data), cancelIfFull, false);
}

bool Microcontroller::sendMulti(int _bank, int _program, TPreset& _data, bool cancelIfFull)
{
	if (_bank != 0) 
	{
		LOG("We do not support writing to flash, attempt to write multi to bank " << _bank << ", program " << _program);
		return true;
	}

	m_multiEditBuffer = _data;

	// Convert array of uint8_t to vector of 24bit TWord
	return sendPreset(_program, presetToDSPWords(_data), cancelIfFull, true);
}

}