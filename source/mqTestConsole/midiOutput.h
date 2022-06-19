#pragma once
#include <vector>

#include "midi.h"

namespace synthLib
{
	struct SMidiEvent;
}

class MidiOutput
{
public:
	MidiOutput();
	void write(const std::vector<uint8_t>& _data);
	void write(const std::vector<synthLib::SMidiEvent>& _events) const;

private:
	void* m_stream = nullptr;
	MidiBufferParser m_parser;
};
