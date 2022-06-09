#include "mqdsp.h"

#include "dspBootCode.h"

namespace mqLib
{
	static dsp56k::DefaultMemoryValidator g_memoryValidator;

	static constexpr dsp56k::TWord g_pMemSize		= 0x4000;	// only $0000 < $1400 for DSP, rest for us
	static constexpr dsp56k::TWord g_bootCodeBase	= 0x2000;

	MqDsp::MqDsp() : m_periphX(nullptr), m_memory(g_memoryValidator, 0x800000), m_dsp(m_memory, &m_periphX, &m_periphNop)
	{
		m_periphX.getEsaiClock().setExternalClockFrequency(44100 * 768);
		m_periphX.getEsaiClock().setSamplerate(44100);

		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction

		// rewrite to work at address g_bootCodeBase instead of $ff0000
		for(uint32_t i=0; i<std::size(g_dspBootCode); ++i)
		{
			uint32_t code = g_dspBootCode[i];
			if((g_dspBootCode[i] & 0xffff00) == 0xff0000)
			{
				code = g_bootCodeBase | (g_dspBootCode[i] & 0xff);
			}

			m_memory.set(dsp56k::MemArea_P, i + g_bootCodeBase, code);
		}

		m_memory.setExternalMemory(0x80000, true);

//		m_memory.saveAssembly("dspBootDisasm.asm", g_bootCodeBase, static_cast<uint32_t>(std::size(g_dspBootCode)), true, true, &m_periphX, nullptr);

		m_dsp.setPC(g_bootCodeBase);
		m_dsp.regs().omr.var |= OMR_MA | OMR_MB | OMR_MC | OMR_MD;

		m_periphX.getEsai().writeEmptyAudioIn(2048);
	}

	void MqDsp::exec()
	{
		m_dsp.exec();
	}

	void MqDsp::dumpPMem(const std::string& _filename)
	{
		m_memory.saveAssembly((_filename + ".asm").c_str(), 0, g_pMemSize, true, false, &m_periphX);
	}
}
