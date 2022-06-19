#pragma once

#include <vector>

#include "../synthLib/midiTypes.h"

class MidiInput
{
public:
	MidiInput();
	bool process(std::vector<synthLib::SMidiEvent>& _events);
private:
	void process(std::vector<synthLib::SMidiEvent>& _events, uint32_t _message);

	void* m_stream = nullptr;
	std::vector<uint8_t> m_sysexBuffer;
	bool m_readSysex = false;
};
