#pragma once

#include <array>

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/hdi08.h"

using namespace dsp56k;

class Syx
{
public:
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
		GLOBAL_CHANNEL             = 0x7c, // 124
		LED_MODE                   = 0x7d, // 125
		LCD_CONTRAST               = 0x7e, // 126
		PANEL_DESTINATION          = 0x79, // 121
		UNK_6d                     = 0x6d, // 109
	};

	enum Page
	{
		PAGE_A = 0,
		PAGE_B = 1,
		PAGE_C = 2
	};

	const int SINGLE = 0x0;
//	const int SINGLE = 0x40;

	Syx (dsp56k::HDI08& hdi08);
	int sendFile (const std::vector<TWord>& preset);
	int sendControlCommand(const ControlCommand command, const int value);
private:
	void send(const Syx::Page page, const int part, const int param, const int value);
	void waitUntilReady();
	void waitUntilBufferEmpty();
	HDI08& m_hdi08;
};
