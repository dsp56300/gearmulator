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

		void process(float** _inputs, float** _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut) override;

		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;

		uint32_t getInternalLatencySamples() const override;

	private:
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void onAudioWritten() override;

		ROMFile m_rom;
		Microcontroller m_syx;
		MidiOutParser m_midiOutParser;
		uint32_t m_numSamplesWritten = 0;
		uint32_t m_numSamplesProcessed = 0;
	};
}
