#pragma once
#include <cstdint>

#include "../synthLib/midiTypes.h"

#include "midiOutParser.h"
#include "romfile.h"
#include "syx.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

namespace virusLib
{
	class Device
	{
	public:
		Device(const char* _romFileName);
		~Device();
		void process(float** _inputs, float** _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut);
		void setBlockSize(size_t _size);

	private:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut);
		void dummyProcess(uint32_t _numSamples);

		std::vector<uint8_t> m_buffer;

		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Peripherals56362 m_periph;

		dsp56k::Memory* m_memory = nullptr;
		dsp56k::DSP* m_dsp = nullptr;
		dsp56k::Jit* m_jit = nullptr;

		ROMFile m_rom;

		Syx m_syx;
		
		std::unique_ptr<dsp56k::DSPThread> m_dspThread;
		std::vector<synthLib::SMidiEvent> m_midiIn;
		size_t m_nextLatency = 0;
		MidiOutParser m_midiOutParser;
	};
}
