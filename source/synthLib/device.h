#pragma once

#include <cstdint>

#include "audioTypes.h"
#include "deviceTypes.h"
#include "../synthLib/midiTypes.h"

#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

namespace synthLib
{
	class Device
	{
	public:
		Device();
		virtual ~Device();
		virtual void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut);

		void setExtraLatencySamples(uint32_t _size);
		uint32_t getExtraLatencySamples() const { return m_extraLatency; }

		virtual uint32_t getInternalLatencyMidiToOutput() const { return 0; }
		virtual uint32_t getInternalLatencyInputToOutput() const { return 0; }

		virtual float getSamplerate() const = 0;
		virtual bool isValid() const = 0;
		virtual bool getState(std::vector<uint8_t>& _state, StateType _type) = 0;
		virtual bool setState(const std::vector<uint8_t>& _state, StateType _type) = 0;

	protected:
		virtual void readMidiOut(std::vector<SMidiEvent>& _midiOut) = 0;
		virtual void processAudio(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _samples) = 0;
		virtual bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) = 0;
		virtual void onAudioWritten() {}

		void dummyProcess(uint32_t _numSamples);
	
	private:
		std::vector<SMidiEvent> m_midiIn;
		uint32_t m_extraLatency = 0;
	};
}
