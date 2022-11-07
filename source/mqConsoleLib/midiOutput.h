#pragma once
#include <vector>

#include "../synthLib/midiBufferParser.h"

#include "midiDevice.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace mqConsoleLib
{
class MidiOutput : public MidiDevice
{
public:
	MidiOutput(const std::string& _deviceName);
	~MidiOutput() override;

	void write(const std::vector<uint8_t>& _data);
	void write(const std::vector<synthLib::SMidiEvent>& _events) const;

	int getDefaultDeviceId() const override;

	bool openDevice(int devId) override;
private:
	void* m_stream = nullptr;
	synthLib::MidiBufferParser m_parser;
};
}