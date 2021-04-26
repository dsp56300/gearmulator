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
		~Device();
		void process(float** _inputs, float** _outputs, size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut);
		void setBlockSize(size_t _size);

	private:
		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Memory m_memory;

		dsp56k::Peripherals56362 m_periph;
		dsp56k::DSP m_dsp;

		ROMFile m_rom;

		Syx m_syx;
		
		std::unique_ptr<dsp56k::DSPThread> m_dspThread;
		std::unique_ptr<std::thread> m_initThread;
		bool m_initDone = false;
		size_t m_nextLatency = 0;
	};
}
