#pragma once

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/hdi08.h"

#include "romfile.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace virusLib
{
class Microcontroller
{
public:
	enum SysexMessageType
	{
		DUMP_SINGLE                = 0x10,
		DUMP_MULTI                 = 0x11,
		REQUEST_SINGLE             = 0x30,
		REQUEST_MULTI              = 0x31,
		REQUEST_BANK_SINGLE        = 0x32,
		REQUEST_BANK_MULTI         = 0x33,
		REQUEST_ARRANGEMENT        = 0x34,			// current Multi + all referenced Singles in the Multi. Only the Singles whose parts are enabled in the Multi are sent
		REQUEST_GLOBAL             = 0x35,			// All parameters not related to a single or a multi
		REQUEST_TOTAL              = 0x36,			// Single RAM Banks + Multi RAM Bank + Global
		REQUEST_CONTROLLER_DUMP    = 0x37,			// Sends the current Single edit buffer as individual controller/polypressure/sysex

		PARAM_CHANGE_A             = 0x70,
		PARAM_CHANGE_B             = 0x71,
		PARAM_CHANGE_C             = 0x72,
	};

    enum ControlCommand
	{
		CC_DEVICE_ID               = 0x5d,  // 93
		CC_PART_ENABLE             = 0x48,  // 72
		CC_PART_MIDI_CHANNEL       = 0x22,  // 34
		CC_PART_OUTPUT_SELECT      = 0x29,  // 41

		CC_PART_MIDI_VOLUME_ENABLE = 0x49,  // 73
		CC_PART_MIDI_VOLUME_INIT   = 0x28,  // 40
		CC_PART_VOLUME             = 0x27,  // 39
		CC_MASTER_VOLUME           = 0x7f,  // 127

		AUDITION				   = 0x01, // 01
		UNK1a                      = 0x1a, // 26
		UNK1b                      = 0x1b, // 27
		UNK1c                      = 0x1c, // 28
		UNK1d                      = 0x1d, // 29
    	PART_BANK_SELECT           = 0x1f, // 31		The change is executed once a program change is received
    	PART_BANK_CHANGE           = 0x20, // 32		The change is executed immediately
    	PART_PROGRAM_CHANGE        = 0x21, // 33
		SECOND_OUTPUT_SELECT       = 0x2d, // 45
		UNK35                      = 0x35, // 53
		UNK36                      = 0x36, // 54
		UNK76                      = 0x4c, // 76
		INPUT_THRU_LEVEL           = 0x5a, // 90
		INPUT_BOOST                = 0x5b, // 91
		MASTER_TUNE                = 0x5c, // 92
		DEVICE_ID                  = 0x5d, // 93
		MIDI_CONTROL_LOW_PAGE      = 0x5e, // 94
		MIDI_CONTROL_HIGH_PAGE     = 0x5f, // 95
		MIDI_ARPEGGIATOR_SEND      = 0x60, // 96
    	MULTI_PROGRAM_CHANGE       = 0x69, // 105
		MIDI_CLOCK_RX              = 0x6a, // 106
		UNK_6d                     = 0x6d, // 109
		PLAY_MODE                  = 0x7a, // 122
		PART_NUMBER                = 0x7b, // 123
		GLOBAL_CHANNEL             = 0x7c, // 124
		LED_MODE                   = 0x7d, // 125
		LCD_CONTRAST               = 0x7e, // 126
		PANEL_DESTINATION          = 0x79, // 121
	};

	enum Page
	{
		PAGE_A = 0x70,
		PAGE_B = 0x71,
		PAGE_C = 0x72,
	};

	enum ProgramType
	{
		SINGLE = 0x40
	};

	enum PlayMode
	{
		PlayModeSingle,
		PlayModeMultiSingle,	// This one is just a different view of multi mode on the hardware, not needed
		PlayModeMulti,
	};

	using TPreset = ROMFile::TPreset;

	explicit Microcontroller(dsp56k::HDI08& hdi08, ROMFile& romFile);

	bool sendPreset(uint32_t program, const std::vector<dsp56k::TWord>& preset, bool cancelIfFull = false, bool isMulti = false) const;
	void sendControlCommand(ControlCommand command, uint8_t value);
	bool sendMIDI(uint8_t a, uint8_t b, uint8_t c, bool cancelIfFull = false);
	bool send(Page page, uint8_t part, uint8_t param, uint8_t value, bool cancelIfFull = false);
	bool sendSysex(const std::vector<uint8_t>& _data, bool _cancelIfFull, std::vector<synthLib::SMidiEvent>& _responses);

	bool writeSingle(uint32_t _bank, uint32_t _program, const TPreset& _data, bool cancelIfFull, bool pendingSingleWrite = false);
	bool writeMulti(uint32_t _bank, uint32_t _program, const TPreset& _data, bool cancelIfFull);
	bool requestMulti(uint32_t _bank, uint32_t _program, TPreset& _data) const;
	bool requestSingle(uint32_t _bank, uint32_t _program, TPreset& _data) const;

	bool needsToWaitForHostBits(char flag1,char flag2) const;
	void sendInitControlCommands();

	void createDefaultState();
	void process(size_t _size);

private:
	void writeHostBitsWithWait(char flag1,char flag2) const;
	void waitUntilReady() const;
	void waitUntilBufferEmpty() const;
	static std::vector<dsp56k::TWord> presetToDSPWords(const TPreset& _preset);
	bool getSingle(uint32_t _bank, uint32_t _preset, TPreset& _result) const;

	bool partBankSelect(uint8_t _part, uint8_t _value, bool _immediatelySelectSingle);
	bool partProgramChange(uint8_t _part, uint8_t _value, bool pendingSingleWrite = false);
	bool multiProgramChange(uint8_t _value);
	bool loadMulti(uint8_t _program, const TPreset& _multi, bool _loadMultiSingles = true);
	bool loadMultiSingle(uint8_t _part);
	bool loadMultiSingle(uint8_t _part, const TPreset& _multi);

	dsp56k::HDI08& m_hdi08;
	ROMFile& m_rom;

	uint8_t m_deviceId = 0;

	TPreset m_multiEditBuffer;

	std::array<uint8_t, 256> m_globalSettings;
	std::vector<std::vector<TPreset>> m_singles;

	// Multi mode
	std::array<TPreset,16> m_singleEditBuffers;
	std::array<uint8_t,16> m_currentBanks;
	std::array<uint8_t,16> m_currentSingles;

	// Single mode
	TPreset m_singleEditBuffer;
	uint8_t m_currentBank = 0;
	uint8_t m_currentSingle = 0;

	// Device does not like if we send everything at once, therefore we delay the send of Singles after sending a Multi
	int m_pendingSingleWrites = 16;
};

}
