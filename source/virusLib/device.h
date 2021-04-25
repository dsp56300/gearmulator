#pragma once
#include "midiTypes.h"
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
		void process(float** _inputs, float** _outputs, size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut);

	private:
		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Memory m_memory;

		dsp56k::Peripherals56362 m_periph;
		dsp56k::DSP m_dsp;

		ROMFile m_rom;

		Syx m_syx;
		
		dsp56k::DSPThread m_dspThread;
	};
}
