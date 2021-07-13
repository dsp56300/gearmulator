#include <vector>
#include <chrono>
#include <thread>

#include "microcontroller.h"

#include "../synthLib/midiTypes.h"

using namespace dsp56k;
using namespace synthLib;

constexpr uint32_t g_sysexPresetHeaderSize = 9;
constexpr uint32_t g_singleRamBankCount = 2;

namespace virusLib
{
Microcontroller::Microcontroller(HDI08& _hdi08, ROMFile& _romFile) : m_hdi08(_hdi08), m_rom(_romFile), m_currentBanks({0}), m_currentSingles({0})
{
	m_globalSettings.fill(0);
	m_currentBanks.fill(0);

	m_rom.getMulti(0, m_multiEditBuffer);

	bool failed = false;

	// read all singles from ROM and copy first ROM banks to RAM banks
	for(uint32_t b=0; b<8 && !failed; ++b)
	{
		std::vector<TPreset> singles;

		for(uint32_t p=0; p<128; ++p)
		{
			const auto bank = b > g_singleRamBankCount ? b - g_singleRamBankCount : b;

			TPreset single;
			m_rom.getSingle(bank, p, single);

			if(ROMFile::getSingleName(single).size() != 10)
			{
				failed = true;
				break;				
			}

			singles.emplace_back(single);
		}

		if(!singles.empty())
			m_singles.emplace_back(std::move(singles));
	}

	m_singleEditBuffer = m_singles[0][0];

	for(auto i=0; i<static_cast<int>(m_singleEditBuffers.size()); ++i)
		m_singleEditBuffers[i] = m_singles[0][i];
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
	sendControlCommand(PLAY_MODE, PlayModeMulti);
//	writeSingle(0, 0, m_singleEditBuffer, false);
	loadMulti(0, m_multiEditBuffer);
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
	TWord buf[] = {0xf47555, static_cast<TWord>(isMulti ? 0x110000 : 0x100000)};
	buf[1] = buf[1] | (program << 8);
	m_hdi08.writeRX(buf, 2);

	// Send to DSP
	m_hdi08.writeRX(preset);

	return true;
}

void Microcontroller::sendControlCommand(const ControlCommand _command, const uint8_t _value)
{
	send(PAGE_C, 0x0, _command, _value);
}

bool Microcontroller::send(const Page _page, const uint8_t _part, const uint8_t _param, const uint8_t _value, bool cancelIfFull/* = false*/)
{
	waitUntilReady();
	if(cancelIfFull && needsToWaitForHostBits(0,1))
		return false;
	writeHostBitsWithWait(0,1);
	TWord buf[] = {0xf4f400, 0x0};
	buf[0] = buf[0] | _page;
	buf[1] = (_part << 16) | (_param << 8) | _value;
	m_hdi08.writeRX(buf, 2);

	if(_page == PAGE_C)
	{
		m_globalSettings[_param] = _value;
	}
	return true;
}

bool Microcontroller::sendMIDI(uint8_t a, uint8_t b, uint8_t c, bool cancelIfFull/* = false*/)
{
	const uint8_t channel = a & 0x0f;
	const uint8_t status = a & 0xf0;

	const auto singleMode = m_globalSettings[PLAY_MODE] == PlayModeSingle;

	if(singleMode && channel != m_globalSettings[GLOBAL_CHANNEL])
		return true;

	switch (status)
	{
	case M_PROGRAMCHANGE:
		{
			TPreset single;

			if(singleMode)
			{
				if(getSingle(m_currentBank, b, single))
				{
					m_currentSingle = b;
					return writeSingle(0, SINGLE, single, cancelIfFull);
				}
			}
			else
			{
				return partProgramChange(channel, b);
			}
		}
		break;
	case M_CONTROLCHANGE:
		switch(b)
		{
		case MC_BANKSELECTLSB:
			if(singleMode)
				m_currentBank = c % m_singles.size();
			else
				partBankSelect(channel, c, false);
			return true;
		default:
			break;
		}
		break;
	default:
		break;
	}

	if(cancelIfFull && (needsToWaitForHostBits(1,1) || m_hdi08.dataRXFull()))
		return false;
	writeHostBitsWithWait(1,1);
	TWord buf[3] = {static_cast<TWord>(a)<<16, static_cast<TWord>(b)<<16, static_cast<TWord>(c)<<16};
	m_hdi08.writeRX(buf,3);
	return true;
}

bool Microcontroller::sendSysex(const std::vector<uint8_t>& _data, bool _cancelIfFull, std::vector<SMidiEvent>& _responses)
{
	const auto manufacturerA = _data[1];
	const auto manufacturerB = _data[2];
	const auto manufacturerC = _data[3];
	const auto productId = _data[4];
	const auto deviceId = _data[5];
	const auto cmd = _data[6];

	if (deviceId != m_deviceId) 
	{
		// ignore messages intended for a different device
		return true;
	}

	auto buildPresetResponse = [&](uint8_t _type, uint8_t _bank, uint8_t _program, const TPreset& _dump)
	{
		SMidiEvent ev;
		auto& response = ev.sysex;

		response.reserve(1024);

		response.push_back(M_STARTOFSYSEX);
		response.push_back(manufacturerA);
		response.push_back(manufacturerB);
		response.push_back(manufacturerC);
		response.push_back(productId);
		response.push_back(deviceId);
		response.push_back(_type);
		response.push_back(_bank);
		response.push_back(_program);

		uint8_t cs = deviceId + 11 + response[7];
		size_t idx = 9;
		for(const auto value : _dump)
		{
			response.push_back(value);
			cs += value;
		}
		response.push_back(cs & 0x7f); // checksum
		response.push_back(M_ENDOFSYSEX);

		_responses.emplace_back(std::move(ev));
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
				return writeSingle(bank, program, dump, _cancelIfFull);
			}
		case DUMP_MULTI: 
			{
				const uint8_t bank = _data[7];
				const uint8_t program = _data[8];
				LOG("Dump Multi, Bank " << bank << ", program " << program);
				TPreset dump;
				std::copy_n(_data.data() + g_sysexPresetHeaderSize, dump.size(), dump.begin());
				return writeMulti(bank, program, dump, _cancelIfFull);
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

				const auto part = _data[7];
				const auto param = _data[8];
				const auto value = _data[9];

				if(page == PAGE_C)
				{
					const auto command = static_cast<ControlCommand>(param);

					switch(command)
					{
					case PLAY_MODE:
						{
							const auto playMode = value;

							switch(playMode)
							{
							case PlayModeSingle:
								{
									m_globalSettings[PLAY_MODE] = playMode;

									LOG("Switch to Single mode");
									return writeSingle(0, SINGLE, m_singleEditBuffer, _cancelIfFull);
								}
							case PlayModeMultiSingle:
							case PlayModeMulti:
								{
									m_globalSettings[PLAY_MODE] = PlayModeMulti;
									return loadMulti(0, m_multiEditBuffer);
								}
							default:
								return true;
							}
						}
					case PART_BANK_SELECT:
						return partBankSelect(part, value, false);
					case PART_BANK_CHANGE:
						return partBankSelect(part, value, true);
					case PART_PROGRAM_CHANGE:
						return partProgramChange(part, value);
					case MULTI_PROGRAM_CHANGE:
						if(part == 0)
						{
							return multiProgramChange(value);
						}
						return true;
					default:
						break;
					}
				}

				return send(page, part, param, value, _cancelIfFull);
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

bool Microcontroller::getSingle(uint32_t _bank, uint32_t _preset, TPreset& _result) const
{
	if(_bank > m_singles.size())
		return false;
	const auto& s = m_singles[_bank];
	
	if(_preset >= s.size())
		return false;

	_result = s[_preset];
	return true;
}

void Microcontroller::waitUntilReady() const
{
	while (!bittest(m_hdi08.readControlRegister(), HDI08::HCR_HRIE)) 
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::this_thread::yield();
	}
}

bool Microcontroller::requestMulti(uint32_t _bank, uint32_t _program, TPreset& _data) const
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

bool Microcontroller::requestSingle(uint32_t _bank, uint32_t _program, TPreset& _data) const
{
	if (_bank == 0)
	{
		// Use single-edit buffer
		_data = m_singleEditBuffers[_program % m_singleEditBuffers.size()];
		return true;
	}

	// Load from flash
	return getSingle(_bank - 1, _program, _data);
}

bool Microcontroller::writeSingle(uint32_t _bank, uint32_t _program, const TPreset& _data, bool cancelIfFull, bool pendingSingleWrite)
{
	if (_bank > 0) 
	{
		if(_bank >= m_singles.size() || _bank >= g_singleRamBankCount)
			return true;	// out of range

		if(_program >= m_singles[_bank].size())
			return true;	// out of range

		m_singles[_bank][_program] = _data;

		return true;
	}

	m_singleEditBuffers[_program % m_singleEditBuffers.size()] = _data;

	LOG("Loading Single " << ROMFile::getSingleName(_data) << " to part " << _program);

	if(pendingSingleWrite)
		return true;

	// Send to DSP
	return sendPreset(_program, presetToDSPWords(_data), cancelIfFull, false);
}

bool Microcontroller::writeMulti(uint32_t _bank, uint32_t _program, const TPreset& _data, bool cancelIfFull)
{
	if (_bank != 0) 
	{
		LOG("We do not support writing to flash, attempt to write multi to bank " << _bank << ", program " << _program);
		return true;
	}

	m_multiEditBuffer = _data;

	LOG("Loading Multi " << ROMFile::getMultiName(_data));

	// Convert array of uint8_t to vector of 24bit TWord
	return sendPreset(_program, presetToDSPWords(_data), cancelIfFull, true);
}

bool Microcontroller::partBankSelect(const uint8_t _part, const uint8_t _value, const bool _immediatelySelectSingle)
{
	m_currentBanks[_part] = _value % m_singles.size();

	if(_immediatelySelectSingle)
		return partProgramChange(_part, m_currentSingles[_part]);

	return true;
}

bool Microcontroller::partProgramChange(const uint8_t _part, const uint8_t _value, bool pendingSingleWrite)
{
	TPreset single;

	if(getSingle(m_currentBanks[_part], _value, single))
	{
		m_currentSingles[_part] = _value;
		return writeSingle(0, _part, single, true, pendingSingleWrite);
	}

	return true;
}

bool Microcontroller::multiProgramChange(uint8_t _value)
{
	TPreset multi;

	if(!m_rom.getMulti(_value, multi))
		return true;

	return loadMulti(_value, multi);
}

bool Microcontroller::loadMulti(uint8_t _program, const TPreset& _multi, bool _loadMultiSingles)
{
	if(!writeMulti(0, _program, _multi, true))
		return false;

	for(uint8_t p=0; p<16; ++p)
		loadMultiSingle(p, _multi);

	m_pendingSingleWrites = 0;

	return true;
}

bool Microcontroller::loadMultiSingle(uint8_t _part)
{
	return loadMultiSingle(_part, m_multiEditBuffer);
}

bool Microcontroller::loadMultiSingle(uint8_t _part, const TPreset& _multi)
{
	const auto partBank = _multi[32 + _part];
	const auto partSingle = _multi[48 + _part];

	partBankSelect(_part, partBank, false);
	return partProgramChange(_part, partSingle, true);
}

void Microcontroller::process(size_t _size)
{
	if(m_pendingSingleWrites < 16 && !m_hdi08.hasDataToSend())
	{
		if(writeSingle(0, m_pendingSingleWrites, m_singleEditBuffers[m_pendingSingleWrites], true))
		{
			++m_pendingSingleWrites;			
		}
	}
}

}
