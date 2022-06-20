#pragma once

#include <string>
#include <vector>

#include "midiDevice.h"

#include "../synthLib/midiTypes.h"

class MidiInput : public MidiDevice
{
public:
	MidiInput(const std::string& _deviceName);
	~MidiInput() override;

	bool process(std::vector<synthLib::SMidiEvent>& _events);

	int getDefaultDeviceId() const override;

	bool openDevice(int _devId) override;
private:
	void process(std::vector<synthLib::SMidiEvent>& _events, uint32_t _message);

	void* m_stream = nullptr;
	std::vector<uint8_t> m_sysexBuffer;
	bool m_readSysex = false;
};
