#include "mqmc.h"

#include <array>
#include <fstream>

#include "rom.h"

#include <cstring>	// memcpy

#include "mqbuildconfig.h"
#include "mc68k/logging.h"

#define MC68K_CLASS mqLib::MqMc
#include "mc68k/musashiEntry.h"

namespace mqLib
{
	constexpr uint32_t g_romAddress = 0x80000;
	constexpr uint32_t g_pcInitial = 0x80130;
	constexpr uint32_t g_memorySize = 0x40000;

	static std::string logChar(char _val)
	{
		std::stringstream ss;
		if(_val > 32 && _val < 127)
			ss << _val;
		else
			ss << "[" << MCHEXN(static_cast<uint8_t>(_val), 2) << "]";
		return ss.str();
	}

	MqMc::MqMc(const ROM& _rom) : m_rom(_rom)
	{
		if(!_rom.isValid())
			return;
		m_romRuntimeData.resize(ROM::size());
		memcpy(m_romRuntimeData.data(), m_rom.getData().data(), ROM::size());

		m_flash.reset(new hwLib::Am29f(m_romRuntimeData.data(), m_romRuntimeData.size(), false, true));

		m_memory.resize(g_memorySize, 0);

		reset();

		setPC(g_pcInitial);

		getPortGP().setWriteTXCallback([this](const mc68k::Port&)
		{
			onPortGPWritten();
		});

		getPortE().setWriteTXCallback([this](const mc68k::Port&)
		{
			onPortEWritten();
		});

		getPortF().setWriteTXCallback([this](const mc68k::Port&)
		{
			onPortFWritten();
		});

		getPortQS().setDirectionChangeCallback([this](const mc68k::Port&)
		{
			onPortQSWritten();
		});

		getPortQS().setWriteTXCallback([this](const mc68k::Port&)
		{
			onPortQSWritten();
		});

#if 0
		dumpAssembly(g_romAddress, g_romSize);
#endif
	}

	MqMc::~MqMc() = default;

//	uint64_t g_instructionCounter = 0;
//	std::deque<uint32_t> g_lastPCs;

	uint32_t MqMc::exec()
	{
//		if(g_instructionCounter >= 17300000)
//			MCLOG("Exec @ " << MCHEX(getPC()));
/*
		g_lastPCs.push_back(getPC());
		if(g_lastPCs.size() > 100)
			g_lastPCs.pop_front();
*/
//		++g_instructionCounter;

		const uint32_t deltaCycles = Mc68k::exec();

		m_hdi08a.exec(deltaCycles);

		if constexpr (g_useVoiceExpansion)
		{
			m_hdi08b.exec(deltaCycles);
			m_hdi08c.exec(deltaCycles);
		}

		m_buttons.processButtons(getPortGP(), getPortE());

		return deltaCycles;
	}

	void MqMc::notifyDSPBooted()
	{
		if(!m_dspResetCompleted)
			MCLOG("DSP has booted");
		m_dspResetCompleted = true;
	}

	uint16_t MqMc::readImm16(uint32_t addr)
	{
		if(addr < g_memorySize)
		{
			return readW(m_memory, addr);
		}

		if(addr >= g_romAddress && addr < g_romAddress + ROM::size())
		{
			const auto r = readW(m_romRuntimeData, addr - g_romAddress);
//			LOG("read16 from ROM addr=" << HEXN(addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}
//		__debugbreak();
		return 0;
	}

	uint16_t MqMc::read16(uint32_t addr)
	{
		if(addr < g_memorySize)
		{
			return readW(m_memory, addr);
		}

		if(addr >= g_romAddress && addr < g_romAddress + ROM::size())
		{
			const auto r = readW(m_romRuntimeData, addr - g_romAddress);
//			LOG("read16 from ROM addr=" << HEXN(addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if (m_hdi08a.isInRange(pa))
			return m_hdi08a.read16(pa);

		if constexpr (g_useVoiceExpansion)
		{
			if (m_hdi08b.isInRange(pa))
				return m_hdi08b.read16(pa);
			if (m_hdi08c.isInRange(pa))
				return m_hdi08c.read16(pa);
		}

//		LOG("read16 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read16(addr);
	}

	uint8_t MqMc::read8(uint32_t addr)
	{
		if(addr < g_memorySize)
			return m_memory[addr];

		if(addr >= g_romAddress && addr < g_romAddress + ROM::size())
			return m_romRuntimeData[addr - g_romAddress];

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if (m_hdi08a.isInRange(pa))
			return m_hdi08a.read8(pa);
		if constexpr (g_useVoiceExpansion)
		{
			if (m_hdi08b.isInRange(pa))
				return m_hdi08b.read8(pa);
			if (m_hdi08c.isInRange(pa))
				return m_hdi08c.read8(pa);
		}

//		LOG("read8 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read8(addr);
	}

	void MqMc::write16(uint32_t addr, uint16_t val)
	{
		// Dump memory if DSP test reaches error state
		if(addr == 0x384A8)
		{
			if(val > 0 && val <= 0xff)
				dumpMemory((std::string("DSPTest_Error_") + std::to_string(val)).c_str());
		}

		if(addr < g_memorySize)
		{
			writeW(m_memory, addr, val);
			return;
		}

		if(addr >= g_romAddress && addr < g_romAddress + ROM::size())
		{
			MCLOG("write16 TO ROM addr=" << MCHEXN(addr, 8) << ", value=" << MCHEXN(val,4) << ", pc=" << MCHEXN(getPC(), 8));
			m_flash->write(addr - g_romAddress, val);
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if (m_hdi08a.isInRange(pa))
		{
			m_hdi08a.write16(pa, val);
			return;
		}

		if constexpr (g_useVoiceExpansion)
		{
			if (m_hdi08b.isInRange(pa))
			{
				m_hdi08b.write16(pa, val);
				return;
			}
			if (m_hdi08c.isInRange(pa))
			{
				m_hdi08c.write16(pa, val);
				return;
			}
		}

		Mc68k::write16(addr, val);
	}

	void MqMc::write8(uint32_t addr, uint8_t val)
	{
		// Dump memory if DSP test reaches error state
		if(addr == 0x384A8)
		{
			if(val > 0)
				dumpMemory((std::string("DSPTest_Error_") + std::to_string(val)).c_str());
		}

		if(addr < g_memorySize)
		{
			m_memory[addr] = val;
			return;
		}

		if(addr >= g_romAddress && addr < g_romAddress + ROM::size())
		{
			MCLOG("write8 TO ROM addr=" << MCHEXN(addr, 8) << ", value=" << MCHEXN(val,2) << " char=" << logChar(val) << ", pc=" << MCHEXN(getPC(), 8));
			m_flash->write(addr - g_romAddress, val);
			return;
		}

//		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << logChar(val) << ", pc=" << HEXN(getPC(), 8));

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);
		if (m_hdi08a.isInRange(pa))
		{
			m_hdi08a.write8(pa, val);
			return;
		}

		if constexpr (g_useVoiceExpansion)
		{
			if (m_hdi08b.isInRange(pa))
			{
				m_hdi08b.write8(pa, val);
				return;
			}
			if (m_hdi08c.isInRange(pa))
			{
				m_hdi08c.write8(pa, val);
				return;
			}
		}

		Mc68k::write8(addr, val);
	}

	void MqMc::dumpMemory(const char* _filename) const
	{
		FILE* hFile = fopen((std::string(_filename) + ".bin").c_str(), "wb");
		fwrite(m_memory.data(), 1, m_memory.size(), hFile);
		fclose(hFile);
	}

	void MqMc::dumpROM(const char* _filename) const
	{
		FILE* hFile = fopen((std::string(_filename) + ".bin").c_str(), "wb");
		fwrite(m_romRuntimeData.data(), 1, ROM::size(), hFile);
		fclose(hFile);
	}

	void MqMc::dumpAssembly(const uint32_t _first, const uint32_t _count)
	{
		std::stringstream ss;
		ss << "mq_68k_" << _first << '-' << (_first + _count) << ".asm";

		std::ofstream f(ss.str(), std::ios::out);

		for(uint32_t i=_first; i<_first + _count;)
		{
			char disasm[64];
			const auto opSize = disassemble(i, disasm);
			f << MCHEXN(i,5) << ": " << disasm << std::endl;
			if(!opSize)
				++i;
			else
				i += opSize;
		}
		f.close();
	}

	void MqMc::onReset()
	{
		dumpMemory("dump_reset");
	}

	uint32_t MqMc::onIllegalInstruction(uint32_t opcode)
	{
		std::stringstream ss;
		ss << "illegalInstruction_" << MCHEXN(getPC(), 8) << "_op" << MCHEXN(opcode,8);
		dumpMemory(ss.str().c_str());

		return Mc68k::onIllegalInstruction(opcode);
	}

	void MqMc::onPortEWritten()
	{
		processLCDandLEDs();
	}

	void MqMc::onPortFWritten()
	{
		processLCDandLEDs();
	}

	void MqMc::onPortGPWritten()
	{
		processLCDandLEDs();
	}

	void MqMc::onPortQSWritten()
	{
		const bool resetIsOutput = getPortQS().getDirection() & (1<<3);
		if(resetIsOutput)
		{
			if(!(getPortQS().read() & (1<<3)))
			{
				if(!m_dspResetRequest)
				{
#ifdef _DEBUG
					MCLOG("Request DSP RESET");
#endif
					m_dspResetRequest = true;
					m_dspResetCompleted = false;
				}
			}
		}
		else
		{
			if(m_dspResetCompleted)
			{
				m_dspResetRequest = false;
				getPortQS().writeRX(1<<3);
			}
		}
#if SUPPORT_NMI_INTERRUPT
		if(getPortQS().getDirection() & (1<<6))
		{
			m_dspInjectNmiRequest = (getPortQS().read() >> 6) & 1;
			if(m_dspInjectNmiRequest)
				int debug=0;
		}
#endif
	}

	void MqMc::processLCDandLEDs()
	{
		if(m_lcd.exec(getPortGP(), getPortF()))
		{
//			const std::string s(&m_lcd.getDdRam().front());
//			if(s.find("SIG") != std::string::npos)
//				dumpMemory("SIG");
		}
		else
		{
			m_leds.exec(getPortF(), getPortGP(), getPortE());
		}
	}
}
