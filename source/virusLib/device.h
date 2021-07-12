#pragma once

#include "../synthLib/midiTypes.h"
#include "../synthLib/device.h"

#include "midiOutParser.h"
#include "romfile.h"
#include "microcontroller.h"

namespace virusLib
{
	class Device final : public synthLib::Device
	{
	public:
		Device(const std::string& _romFileName);

		float getSamplerate() const override;
		bool isValid() const override;

	private:
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;

		ROMFile m_rom;
		Microcontroller m_syx;
		MidiOutParser m_midiOutParser;
	};
}
