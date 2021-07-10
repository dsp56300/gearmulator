#pragma once

#include "../synthLib/midiTypes.h"
#include "../synthLib/device.h"

#include "midiOutParser.h"
#include "romfile.h"
#include "syx.h"

namespace virusLib
{
	class Device final : public synthLib::Device
	{
	public:
		Device(const std::string& _romFileName);

		float getSamplerate() override;

	private:
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;

		ROMFile m_rom;
		Syx m_syx;
		MidiOutParser m_midiOutParser;
	};
}
