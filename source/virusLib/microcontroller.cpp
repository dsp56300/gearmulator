#include <vector>
#include <chrono>
#include <thread>
#include <cstring> // memcpy

#include "microcontroller.h"

#include "../synthLib/midiTypes.h"

using namespace dsp56k;
using namespace synthLib;

constexpr uint32_t g_sysexPresetHeaderSize = 9;
constexpr uint32_t g_singleRamBankCount = 2;
constexpr uint32_t g_presetsPerBank = 128;

constexpr uint8_t g_pageC_global[]     = {45,63,64,65,66,67,68,69,70,85,86,87,90,91,92,93,94,95,96,97,98,99,105,106,110,111,112,113,114,115,116,117,118,120,121,122,123,124,125,126,127};
constexpr uint8_t g_pageC_multi[]      = {5,6,7,8,9,10,11,12,13,14,22,31,32,33,34,35,36,37,38,39,40,41,72,73,74,75,77,78};
constexpr uint8_t g_pageC_multiPart[]  = {31,32,33,34,35,36,37,38,39,40,41,72,73,74,75,77,78};

namespace virusLib
{
Microcontroller::Microcontroller(HDI08& _hdi08, ROMFile& _romFile) : m_hdi08(_hdi08), m_rom(_romFile)
{
	if(!_romFile.isValid())
		return;

	m_globalSettings.fill(0);

	m_rom.getMulti(0, m_multiEditBuffer);

	bool failed = false;

	// read all singles from ROM and copy first ROM banks to RAM banks
	for(uint32_t b=0; b<8 && !failed; ++b)
	{
		std::vector<TPreset> singles;

		for(uint32_t p=0; p<g_presetsPerBank; ++p)
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

	if(!m_singles.empty())
	{
		const auto& singles = m_singles[0];

		if(!singles.empty())
		{
			m_singleEditBuffer = singles[0];

			for(auto i=0; i<static_cast<int>(std::min(singles.size(), m_singleEditBuffers.size())); ++i)
				m_singleEditBuffers[i] = singles[i];
		}
	}
}

void Microcontroller::sendInitControlCommands()
{
	sendControlCommand(MIDI_CLOCK_RX, 0x1);				// Enable MIDI clock receive
	sendControlCommand(GLOBAL_CHANNEL, 0x0);			// Set global midi channel to 0
	sendControlCommand(MIDI_CONTROL_LOW_PAGE, 0x1);		// Enable midi CC to edit parameters on page A
	sendControlCommand(MIDI_CONTROL_HIGH_PAGE, 0x1);	// Enable poly pressure to edit parameters on page B
	sendControlCommand(MASTER_VOLUME, 100);			// Set master volume to 100
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

bool Microcontroller::sendPreset(uint8_t program, const std::vector<TWord>& preset, bool cancelIfFull, bool isMulti)
{
	if(m_hdi08.hasDataToSend() || needsToWaitForHostBits(0,1))
	{
		m_pendingPresetWrites.emplace_back(SPendingPresetWrite{program, isMulti, preset});
		return true;
	}

	writeHostBitsWithWait(0,1);
	// Send header
	TWord buf[] = {0xf47555, static_cast<TWord>(isMulti ? 0x110000 : 0x100000)};
	buf[1] = buf[1] | (program << 8);
	m_hdi08.writeRX(buf, 2);

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
			applyToSingleEditBuffer(PAGE_A, singleMode ? channel : 0, b, c);
			break;
		}
		break;
	case M_POLYPRESSURE:
		applyToSingleEditBuffer(PAGE_B, singleMode ? channel : 0, b, c);
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

	if (deviceId != m_globalSettings[DEVICE_ID])
	{
		// ignore messages intended for a different device
		return true;
	}

	auto buildResponseHeader = [&](SMidiEvent& ev)
	{
		auto& response = ev.sysex;

		response.reserve(1024);

		response.push_back(M_STARTOFSYSEX);
		response.push_back(manufacturerA);
		response.push_back(manufacturerB);
		response.push_back(manufacturerC);
		response.push_back(productId);
		response.push_back(deviceId);
	};

	auto buildPresetResponse = [&](const uint8_t _type, const uint8_t _bank, const uint8_t _program, const TPreset& _dump)
	{
		SMidiEvent ev;
		auto& response = ev.sysex;

		buildResponseHeader(ev);

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

	auto buildSingleResponse = [&](const uint8_t _bank, const uint8_t _program)
	{
		TPreset dump;
		const auto res = requestSingle(_bank, _program, dump);
		if(res)
			buildPresetResponse(DUMP_SINGLE, _bank, _program, dump);
	};

	auto buildMultiResponse = [&](const uint8_t _bank, const uint8_t _program)
	{
		TPreset dump;
		const auto res = requestMulti(_bank, _program, dump);
		if(res)
			buildPresetResponse(DUMP_MULTI, _bank, _program, dump);
	};

	auto buildSingleBankResponse = [&](const uint8_t _bank)
	{
		if(_bank > 0 && _bank < m_singles.size())
		{
			// eat this, host, whoever you are. 128 single packets
			for(uint8_t i=0; i<m_singles[_bank].size(); ++i)
			{
				TPreset data;
				const auto res = requestSingle(_bank, i, data);
				buildPresetResponse(DUMP_SINGLE, _bank, i, data);
			}
		}		
	};

	auto buildMultiBankResponse = [&](const uint8_t _bank)
	{
		if(_bank == 1)
		{
			// eat this, host, whoever you are. 128 multi packets
			for(uint8_t i=0; i<g_presetsPerBank; ++i)
			{
				TPreset data;
				const auto res = requestMulti(_bank, i, data);
				buildPresetResponse(DUMP_MULTI, _bank, i, data);
			}
		}
	};

	auto buildGlobalResponse = [&](const uint8_t _param)
	{
		SMidiEvent ev;
		auto& response = ev.sysex;

		buildResponseHeader(ev);

		response.push_back(PARAM_CHANGE_C);
		response.push_back(0);	// part = 0
		response.push_back(_param);
		response.push_back(m_globalSettings[_param]);
		response.push_back(M_ENDOFSYSEX);
	};

	auto buildGlobalResponses = [&]()
	{
		for (auto globalParam : g_pageC_global)
			buildGlobalResponse(globalParam);
	};

	auto buildTotalResponse = [&]()
	{
		buildGlobalResponses();
		buildSingleBankResponse(1);
		buildSingleBankResponse(2);
		buildMultiBankResponse(1);
	};

	auto buildArrangementResponse = [&]()
	{
		buildMultiResponse(0, 0);

		for(uint8_t p=0; p<16; ++p)
			buildPresetResponse(DUMP_SINGLE, 0, p, m_singleEditBuffers[p]);
	};

	switch (cmd)
	{
		case DUMP_SINGLE: 
			{
				const uint8_t bank = _data[7];
				const uint8_t program = _data[8];
				LOG("Received Single dump, Bank " << (int)bank << ", program " << (int)program);
				TPreset dump;
				std::copy_n(_data.data() + g_sysexPresetHeaderSize, dump.size(), dump.begin());
				return writeSingle(bank, program, dump, _cancelIfFull);
			}
		case DUMP_MULTI:
			{
				const uint8_t bank = _data[7];
				const uint8_t program = _data[8];
				LOG("Received Multi dump, Bank " << (int)bank << ", program " << (int)program);
				TPreset dump;
				std::copy_n(_data.data() + g_sysexPresetHeaderSize, dump.size(), dump.begin());
				return writeMulti(bank, program, dump, _cancelIfFull);
			}
		case REQUEST_SINGLE:
			{
				const uint8_t bank = _data[7];
				const uint8_t program = _data[8];
				LOG("Request Single, Bank " << (int)bank << ", program " << (int)program);
				buildSingleResponse(bank, program);
				break;
			}
		case REQUEST_MULTI:
			{
				const uint8_t bank = _data[7];
				const uint8_t program = _data[8];
				LOG("Request Multi, Bank " << (int)bank << ", program " << (int)program);
				buildMultiResponse(bank, program);
				break;
			}
		case REQUEST_BANK_SINGLE:
			{
				const uint8_t bank = _data[7];
				buildSingleBankResponse(bank);
				break;
			}
		case REQUEST_BANK_MULTI:
			{
				const uint8_t bank = _data[7];
				buildMultiBankResponse(bank);
				break;
			}
		case REQUEST_GLOBAL:
			buildGlobalResponses();
			break;
		case REQUEST_TOTAL:
			buildTotalResponse();
			break;
		case REQUEST_ARRANGEMENT:
			buildArrangementResponse();
			break;
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
					applyToMultiEditBuffer(part, param, value);

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
				else
				{
					applyToSingleEditBuffer(page, part, param, value);
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

bool Microcontroller::requestMulti(uint8_t _bank, uint8_t _program, TPreset& _data) const
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

bool Microcontroller::requestSingle(uint8_t _bank, uint8_t _program, TPreset& _data) const
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

bool Microcontroller::writeSingle(uint8_t _bank, uint8_t _program, const TPreset& _data, bool cancelIfFull)
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

	if(_program == SINGLE)
		m_singleEditBuffer = _data;
	else
		m_singleEditBuffers[_program % m_singleEditBuffers.size()] = _data;

	LOG("Loading Single " << ROMFile::getSingleName(_data) << " to part " << (int)_program);

	// Send to DSP
	return sendPreset(_program, presetToDSPWords(_data), cancelIfFull, false);
}

bool Microcontroller::writeMulti(uint8_t _bank, uint8_t _program, const TPreset& _data, bool cancelIfFull)
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
	m_multiEditBuffer[MD_PART_BANK_NUMBER + _part] = _value % m_singles.size();

	if(_immediatelySelectSingle)
		return partProgramChange(_part, m_multiEditBuffer[MD_PART_PROGRAM_NUMBER + _part]);

	return true;
}

bool Microcontroller::partProgramChange(const uint8_t _part, const uint8_t _value)
{
	TPreset single;

	const auto bank = m_multiEditBuffer[MD_PART_BANK_NUMBER + _part];

	if(getSingle(bank, _value, single))
	{
		m_multiEditBuffer[MD_PART_PROGRAM_NUMBER + _part] = _value;
		return writeSingle(0, _part, single, true);
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

bool Microcontroller::loadMulti(uint8_t _program, const TPreset& _multi)
{
	if(!writeMulti(0, _program, _multi, true))
		return false;

	for(uint8_t p=0; p<16; ++p)
		loadMultiSingle(p, _multi);

	return true;
}

bool Microcontroller::loadMultiSingle(uint8_t _part)
{
	return loadMultiSingle(_part, m_multiEditBuffer);
}

bool Microcontroller::loadMultiSingle(uint8_t _part, const TPreset& _multi)
{
	const auto partBank = _multi[MD_PART_BANK_NUMBER + _part];
	const auto partSingle = _multi[MD_PART_PROGRAM_NUMBER + _part];

	partBankSelect(_part, partBank, false);
	return partProgramChange(_part, partSingle);
}

void Microcontroller::process(size_t _size)
{
	if(!m_pendingPresetWrites.empty() && !m_hdi08.hasDataToSend())
	{
		const auto preset = m_pendingPresetWrites.front();
		m_pendingPresetWrites.pop_front();

		sendPreset(preset.program, preset.data, false, preset.isMulti);
	}
}

bool Microcontroller::getState(std::vector<unsigned char>& _state, const StateType _type)
{
	const auto deviceId = m_globalSettings[DEVICE_ID];

	std::vector<SMidiEvent> responses;

	if(_type == StateTypeGlobal)
		sendSysex({M_STARTOFSYSEX, 0x00, 0x20, 0x33, 0x01, deviceId, REQUEST_TOTAL, M_ENDOFSYSEX}, false, responses);

	sendSysex({M_STARTOFSYSEX, 0x00, 0x20, 0x33, 0x01, deviceId, REQUEST_ARRANGEMENT, M_ENDOFSYSEX}, false, responses);

	if(responses.empty())
		return false;

	for (const auto& response : responses)
	{
		assert(!response.sysex.empty());
		_state.insert(_state.end(), response.sysex.begin(), response.sysex.end());		
	}

	return true;
}

bool Microcontroller::setState(const std::vector<unsigned char>& _state, const StateType _type)
{
	std::vector<SMidiEvent> events;

	for(size_t i=0; i<_state.size(); ++i)
	{
		if(_state[i] == 0xf0)
		{
			const auto begin = i;

			for(++i; i<_state.size(); ++i)
			{
				if(_state[i] == 0xf7)
				{
					SMidiEvent ev;
					ev.sysex.resize(i + 1 - begin);
					memcpy(&ev.sysex[0], &_state[begin], ev.sysex.size());
					events.emplace_back(ev);
					break;
				}
			}
		}
	}

	if(events.empty())
		return false;

	std::vector<SMidiEvent> unusedResponses;

	for (const auto& event : events)
	{
		sendSysex(event.sysex, false, unusedResponses);
		unusedResponses.clear();
	}

	return true;
}

void Microcontroller::applyToSingleEditBuffer(const Page _page, const uint8_t _part, const uint8_t _param, const uint8_t _value)
{
	if(m_globalSettings[PLAY_MODE] == PlayModeSingle)
		applyToSingleEditBuffer(m_singleEditBuffer, _page, _param, _value);
	else
		applyToSingleEditBuffer(m_singleEditBuffers[_part], _page, _param, _value);
}

void Microcontroller::applyToSingleEditBuffer(TPreset& _single, const Page _page, const uint8_t _param, const uint8_t _value)
{
	// The manual does not have a Single dump specification, therefore I assume that its a 1:1 mapping of pages A and B

	const uint32_t offset = (_page - PAGE_A) * g_presetsPerBank + _param;

	if(offset >= _single.size())
		return;

	_single[offset] = _value;
}

void Microcontroller::applyToMultiEditBuffer(const uint8_t _part, const uint8_t _param, const uint8_t _value)
{
	// TODO: This is horrible. We need to remap everything
}

}
