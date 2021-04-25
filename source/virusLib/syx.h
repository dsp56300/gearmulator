#pragma once

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/hdi08.h"

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

	explicit Syx (dsp56k::HDI08& hdi08);
	void sendFile (const std::vector<dsp56k::TWord>& preset) const;
	void sendControlCommand(ControlCommand command, int value) const;
	void sendMIDI(int a,int b,int c) const;
	void send(Page page, int part, int param, int value) const;
private:
	void writeHostBitsWithWait(char flag1,char flag2) const;
	void waitUntilReady() const;
	void waitUntilBufferEmpty() const;
	dsp56k::HDI08& m_hdi08;
};
