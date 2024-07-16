#include "n2xdsp.h"

#include "n2xhardware.h"
#include "dsp56kEmu/dspthread.h"

namespace n2x
{
	static constexpr dsp56k::TWord g_xyMemSize			= 0x40000;
	static constexpr dsp56k::TWord g_externalMemAddr	= 0x20000;
	static constexpr dsp56k::TWord g_pMemSize			= 0x2000;	// only $0000 < $1400 for DSP, rest for us
	static constexpr dsp56k::TWord g_bootCodeBase		= 0x1500;

	// DSP56362 bootloader
	static constexpr uint32_t g_dspBootCode[] =
	{
		0x350013, 0x0afa23, 0xff0035, 0x0afa22,
		0xff000e, 0x0afa01, 0xff0022, 0x0afa20,
		0xff005e, 0x61f400, 0xff1000, 0x050c8f,
		0x0afa00, 0xff0021, 0x31a900, 0x0afa01,
		0xff0012, 0x0ad161, 0x04d191, 0x019191,
		0xff0013, 0x044894, 0x019191, 0xff0016,
		0x045094, 0x221100, 0x06c800, 0xff001f,
		0x019191, 0xff001c, 0x009814, 0x000000,
		0x050c5a, 0x050c5d, 0x62f400, 0xd00000,
		0x08f4b8, 0xd00409, 0x060680, 0xff0029,
		0x07da8a, 0x0c1c10, 0x219000, 0x219100,
		0x06c800, 0xff0033, 0x060380, 0xff0031,
		0x07da8a, 0x0c1c10, 0x07588c, 0x000000,
		0x050c46, 0x0afa02, 0xff005c, 0x0afa01,
		0xff003e, 0x0afa00, 0xff0046, 0x08f484,
		0x000038, 0x050c0b, 0x0afa20, 0xff0043,
		0x08f484, 0x005018, 0x050c06, 0x08f484,
		0x000218, 0x050c03, 0x08f484, 0x001c1e,
		0x0a8426, 0x0a8380, 0xff0049, 0x084806,
		0x0a8380, 0xff004c, 0x085006, 0x221100,
		0x06c800, 0xff0059, 0x0a83a0, 0xff0058,
		0x0a8383, 0xff0052, 0x00008c, 0x050c03,
		0x085846, 0x000000, 0x0000b9, 0x0ae180,
		0x0afa01, 0xff005f, 0x050c00, 0x66f41b,
		0xff0090, 0x0503a6, 0x04cfdd, 0x013f03,
		0x013e23, 0x045517, 0x060980, 0xff008b,
		0x07de85, 0x07de84, 0x07de86, 0x300013,
		0x70f400, 0x001600, 0x06d820, 0x4258a2,
		0x320013, 0x72f400, 0x000c00, 0x06da20,
		0x075a86, 0x300013, 0x06d800, 0xff007d,
		0x54e000, 0x200063, 0x200018, 0x5cd800,
		0x200043, 0x200018, 0x320013, 0x06da00,
		0xff0083, 0x07da8c, 0x200053, 0x200018,
		0x022d07, 0x08d73c, 0x0d104a, 0x000005,
		0x013d03, 0x00008c, 0x050c02, 0x017d03,
		0x000200, 0x000086
	};

	namespace
	{
		dsp56k::DefaultMemoryValidator g_memValidator;
	}
	DSP::DSP(Hardware& _hw, mc68k::Hdi08& _hdiUc, const uint32_t _index)
	: m_hardware(_hw)
	, m_hdiUC(_hdiUc)
	, m_index(_index)
	, m_name(_index ? "DSP B" : "DSP A")
	, m_memory(g_memValidator, g_pMemSize, g_xyMemSize, g_externalMemAddr)
	, m_dsp(m_memory, &m_periphX, &m_periphNop)
	{
		if(!_hw.isValid())
			return;

		m_periphX.getEsaiClock().setExternalClockFrequency(44100 * 768); // TODO
		m_periphX.getEsaiClock().setSamplerate(44100); // TODO
		m_periphX.getEsaiClock().setClockSource(dsp56k::EsaiClock::ClockSource::Instructions);	// TODO

		auto config = m_dsp.getJit().getConfig();

		config.aguSupportBitreverse = true;
		config.linkJitBlocks = true;
		config.dynamicPeripheralAddressing = false;
#ifdef _DEBUG
		config.debugDynamicPeripheralAddressing = true;
#endif
		config.maxInstructionsPerBlock = 0;

		m_dsp.getJit().setConfig(config);

		// fill P memory with something that reminds us if we jump to garbage
		for(dsp56k::TWord i=0; i<m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x000200);	// debug instruction
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		// rewrite bootloader to work at address g_bootCodeBase instead of $ff0000
		for(uint32_t i=0; i<std::size(g_dspBootCode); ++i)
		{
			uint32_t code = g_dspBootCode[i];
			if((g_dspBootCode[i] & 0xffff00) == 0xff0000)
			{
				code = g_bootCodeBase | (g_dspBootCode[i] & 0xff);
			}

			m_memory.set(dsp56k::MemArea_P, i + g_bootCodeBase, code);
			m_dsp.getJit().notifyProgramMemWrite(i + g_bootCodeBase);
		}

		// set OMR pins so that bootcode wants program data via HDI08 RX
		m_dsp.setPC(g_bootCodeBase);
		m_dsp.regs().omr.var |= OMR_MA | OMR_MB | OMR_MC | OMR_MD;

		m_hdiUC.setRxEmptyCallback([&](const bool _needMoreData)
		{
			onUCRxEmpty(_needMoreData);
		});
		m_hdiUC.setWriteTxCallback([&](const uint32_t _word)
		{
			hdiTransferUCtoDSP(_word);
		});
		m_hdiUC.setWriteIrqCallback([&](const uint8_t _irq)
		{
			hdiSendIrqToDSP(_irq);
		});
		m_hdiUC.setReadIsrCallback([&](const uint8_t _isr)
		{
			return hdiUcReadIsr(_isr);
		});

#if DSP56300_DEBUGGER
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str(), std::make_shared<dsp56kDebugger::Debugger>(m_dsp.dsp())));
#else
		m_thread.reset(new dsp56k::DSPThread(dsp(), m_name.c_str()));
#endif

		m_thread->setLogToStdout(false);
	}

	void DSP::onUCRxEmpty(bool _needMoreData)
	{
	}

	void DSP::hdiTransferUCtoDSP(const uint32_t _word)
	{
		LOG('[' << m_name << "] toDSP writeRX=" << HEX(_word));
		hdi08().writeRX(&_word, 1);
	}

	void DSP::hdiSendIrqToDSP(uint8_t _irq)
	{
		dsp().injectExternalInterrupt(_irq);

		while(dsp().hasPendingInterrupts())
		{
			std::this_thread::yield();
		}
	}

	uint8_t DSP::hdiUcReadIsr(uint8_t _isr)
	{
		return _isr;
	}

	bool DSP::hdiTransferDSPtoUC()
	{
		if (m_hdiUC.canReceiveData() && hdi08().hasTX())
		{
			const auto v = hdi08().readTX();
			LOG('[' << m_name << "] HDI dsp2uc=" << HEX(v));
			m_hdiUC.writeRx(v);
			return true;
		}
		return false;
	}

}
