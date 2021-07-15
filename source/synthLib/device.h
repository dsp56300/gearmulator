#pragma once

#include <cstdint>

#include "deviceTypes.h"
#include "../synthLib/midiTypes.h"

#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

namespace synthLib
{
	class Device
	{
	public:
		Device(uint32_t _memorySize, uint32_t _externalMemStartAddress);
		virtual ~Device();
		virtual void process(float** _inputs, float** _outputs, size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut);
		void setBlockSize(size_t _size);

		void startDSPThread();

		virtual float getSamplerate() const = 0;
		virtual bool isValid() const = 0;
		virtual bool getState(std::vector<uint8_t>& _state, StateType _type) = 0;
		virtual bool setState(const std::vector<uint8_t>& _state, StateType _type) = 0;

	protected:
		virtual void readMidiOut(std::vector<SMidiEvent>& _midiOut) = 0;
		virtual bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) = 0;

		void dummyProcess(uint32_t _numSamples);

		dsp56k::HDI08& getHDI08() { return m_periph.getHDI08(); }
		dsp56k::Peripherals56362& getPeriph() { return m_periph; }
		dsp56k::DSP& getDSP() { return *m_dsp; }
	
	private:
		std::vector<uint8_t> m_buffer;

		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Peripherals56362 m_periph;

		dsp56k::Memory* m_memory = nullptr;
		dsp56k::DSP* m_dsp = nullptr;
		dsp56k::Jit* m_jit = nullptr;

		std::unique_ptr<dsp56k::DSPThread> m_dspThread;
		std::vector<SMidiEvent> m_midiIn;
		size_t m_nextLatency = 0;
	};
}
