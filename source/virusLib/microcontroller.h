#pragma once

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/hdi08.h"
#include "romfile.h"

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
		UNK35                      = 0x35, // 53
		UNK36                      = 0x36, // 54
		SECOND_OUTPUT_SELECT       = 0x2d, // 45
		UNK76                      = 0x4c, // 76
		INPUT_THRU_LEVEL           = 0x5a, // 90
		INPUT_BOOST                = 0x5b, // 91
		MASTER_TUNE                = 0x5c, // 92
		DEVICE_ID                  = 0x5d, // 93
		MIDI_CONTROL_LOW_PAGE      = 0x5e, // 94
		MIDI_CONTROL_HIGH_PAGE     = 0x5f, // 95
		MIDI_ARPEGGIATOR_SEND      = 0x60, // 96
		MIDI_CLOCK_RX              = 0x6a, // 106
		PLAY_MODE                  = 0x7a, // 122
		PART_NUMBER                = 0x7b, // 123
		GLOBAL_CHANNEL             = 0x7c, // 124
		LED_MODE                   = 0x7d, // 125
		LCD_CONTRAST               = 0x7e, // 126
		PANEL_DESTINATION          = 0x79, // 121
		UNK_6d                     = 0x6d, // 109
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
	void sendControlCommand(ControlCommand command, int value) const;
	bool sendMIDI(int a,int b,int c, bool cancelIfFull = false);
	bool send(Page page, int part, int param, int value, bool cancelIfFull = false) const;
	bool sendSysex(const std::vector<uint8_t>& _data, bool _cancelIfFull, std::vector<uint8_t>& _response);

	bool sendSingle(int _bank, int _program, TPreset& _data, bool cancelIfFull);
	bool sendMulti(int _bank, int _program, TPreset& _data, bool cancelIfFull);
	bool requestMulti(int _bank, int _program, TPreset& _data) const;
	bool requestSingle(int _bank, int _program, TPreset& _data) const;

	bool needsToWaitForHostBits(char flag1,char flag2) const;
	void sendInitControlCommands();

	void createDefaultState();

private:
	void writeHostBitsWithWait(char flag1,char flag2) const;
	void waitUntilReady() const;
	void waitUntilBufferEmpty() const;
	static std::vector<dsp56k::TWord> presetToDSPWords(const TPreset& _preset);

	dsp56k::HDI08& m_hdi08;
	ROMFile& m_rom;

	uint8_t m_deviceId = 0;

	TPreset m_multiEditBuffer;

	std::array<TPreset,16> m_singleEditBuffers;	// Multi mode
	TPreset m_singleEditBuffer;					// Single mode

	std::array<uint32_t,16> m_currentBank;
};

}
