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
		CC_DEVICE_ID = 93,
		CC_PART_ENABLE = 72,
		CC_PART_MIDI_CHANNEL = 34,
		CC_PART_OUTPUT_SELECT = 41,

		CC_PART_MIDI_VOLUME_ENABLE = 73,
		CC_PART_MIDI_VOLUME_INIT = 40,
		CC_PART_VOLUME = 39,
		CC_MASTER_VOLUME = 127
	};

	enum Page
	{
		PAGE_A = 0,
		PAGE_B = 1,
		PAGE_C = 2
	};

	const int SINGLE = 0x40;

	Syx (dsp56k::HDI08& hdi08);
	int sendFile (const char* _path);
	int sendControlCommand(const ControlCommand command, const int value);
private:
	void send(const Syx::Page page, const int part, const int param, const int value);
	void waitUntilReady();
	void waitUntilBufferEmpty();
	HDI08& m_hdi08;
};
