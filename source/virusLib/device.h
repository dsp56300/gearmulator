#pragma once
#include "romfile.h"
#include "../dsp56300/source/dsp56kEmu/dspthread.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

namespace virusLib
{
	class Device
	{
	public:
		Device(const char* _romFileName);

	private:
		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Memory m_memory;

		dsp56k::Peripherals56362 m_periph;
		dsp56k::DSP m_dsp;

		ROMFile m_rom;
		dsp56k::DSPThread m_dspThread;
	};
}
