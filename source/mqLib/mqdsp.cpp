#include "mqdsp.h"

#include "dspBootCode.h"

namespace mqLib
{
	static dsp56k::DefaultMemoryValidator g_memoryValidator;

	static constexpr dsp56k::TWord g_bridgedAddr	= 0x080000;	// start of external SRAM, mapped to X and Y
	static constexpr dsp56k::TWord g_xyMemSize		= 0x800000;	// due to weird AAR mapping we just allocate enough so that everything fits into it
	static constexpr dsp56k::TWord g_pMemSize		= 0x2000;	// only $0000 < $1400 for DSP, rest for us
	static constexpr dsp56k::TWord g_bootCodeBase	= 0x1500;

	MqDsp::MqDsp() : m_periphX(nullptr), m_memory(g_memoryValidator, g_pMemSize, g_xyMemSize, g_bridgedAddr), m_dsp(m_memory, &m_periphX, &m_periphNop)
	{
		m_periphX.getEsaiClock().setExternalClockFrequency(44100 * 768); // measured as being roughly 33,9MHz, this should be exact
		m_periphX.getEsaiClock().setSamplerate(44100); // verified

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = true;
		config.maxInstructionsPerBlock = 0;	// TODO: needs to be 1 if DSP factory tests are run, to be investigated

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction

		// rewrite bootloader to work at address g_bootCodeBase instead of $ff0000
		for(uint32_t i=0; i<std::size(g_dspBootCode); ++i)
		{
			uint32_t code = g_dspBootCode[i];
			if((g_dspBootCode[i] & 0xffff00) == 0xff0000)
			{
				code = g_bootCodeBase | (g_dspBootCode[i] & 0xff);
			}

			m_memory.set(dsp56k::MemArea_P, i + g_bootCodeBase, code);
		}

//		m_memory.saveAssembly("dspBootDisasm.asm", g_bootCodeBase, static_cast<uint32_t>(std::size(g_dspBootCode)), true, true, &m_periphX, nullptr);

		// set OMR pins so that bootcode wants program data via HDI08 RX
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

	void MqDsp::dumpXYMem(const std::string& _filename)
	{
		m_memory.save((_filename + "_X.txt").c_str(), dsp56k::MemArea_X);
		m_memory.save((_filename + "_Y.txt").c_str(), dsp56k::MemArea_Y);
	}
}
