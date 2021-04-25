#include "device.h"

#include "romfile.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace virusLib
{
	// 128k words beginning at 0x200000
	constexpr TWord g_externalMemStart	= 0x020000;
	constexpr TWord g_memorySize		= 0x040000;

	Device::Device(const char* _romFileName)
		: m_memory(m_memoryValidator, g_memorySize)
		, m_dsp(m_memory, &m_periph, &m_periph)
		, m_rom(_romFileName)
		, m_dspThread(m_dsp)
	{
		m_memory.setExternalMemory(g_externalMemStart, true);

		auto loader = m_rom.bootDSP(m_dsp, m_periph);
		loader.join();
	}
}
