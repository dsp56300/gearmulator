#include "mqmc.h"

#include <array>
#include <fstream>

#include "rom.h"

#include "dsp56kEmu/logging.h"

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
			ss << "[" << HEXN(static_cast<uint8_t>(_val), 2) << "]";
		return ss.str();
	}

	MqMc::MqMc(const ROM& _rom) : m_rom(_rom)
	{
		m_romRuntimeData.resize(m_rom.getSize());
		memcpy(&m_romRuntimeData[0], m_rom.getData(), m_rom.getSize());

		m_memory.resize(g_memorySize, 0);

		reset();

		setPC(g_pcInitial);

#if 0
		dumpAssembly(g_romAddress, g_romSize);
#endif
	}

	MqMc::~MqMc() = default;

	uint32_t MqMc::exec()
	{
		if(getPC() == 0x80718)
		{
			// TODO: hack to prevent getting stuck here
			m_memory[0x170] = 32;
		}

		const uint32_t deltaCycles = Mc68k::exec();

		const bool resetIsOutput = getPortQS().getDirection() & (1<<3);
		if(resetIsOutput)
		{
			if(!(getPortQS().read() & (1<<3)))
			{
				if(!m_dspResetRequest)
				{
					LOG("Request DSP RESET");
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

		if(getPortQS().getDirection() & (1<<6))
		{
			m_dspInjectNmiRequest = (getPortQS().read() >> 6) & 1;
			if(m_dspInjectNmiRequest)
				int debug=0;
		}

		m_buttons.processButtons(getPortGP(), getPortE());

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

		return deltaCycles;
	}

	void MqMc::notifyDSPBooted()
	{
		if(!m_dspResetCompleted)
			LOG("DSP has booted");
		m_dspResetCompleted = true;
	}

	uint16_t MqMc::read16(uint32_t addr)
	{
		if(addr < g_memorySize)
		{
			return readW(m_memory, addr);
		}

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			const auto r = readW(m_romRuntimeData, addr - g_romAddress);
//			LOG("read16 from ROM addr=" << HEXN(addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}
		
//		LOG("read16 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read16(addr);
	}

	uint8_t MqMc::read8(uint32_t addr)
	{
		if(addr < g_memorySize)
			return m_memory[addr];

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
			return m_romRuntimeData[addr - g_romAddress];

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

		if(addr < m_memory.size())
		{
			writeW(m_memory, addr, val);
			return;
		}

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			LOG("write16 TO ROM addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4) << ", pc=" << HEXN(getPC(), 8));
			writeW(m_romRuntimeData, addr - g_romAddress, val);
			return;
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

		if(addr < m_memory.size())
		{
			m_memory[addr] = val;
			return;
		}

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			LOG("write8 TO ROM addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << logChar(val) << ", pc=" << HEXN(getPC(), 8));
			m_romRuntimeData[addr - g_romAddress] = val;
			return;
		}

//		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << logChar(val) << ", pc=" << HEXN(getPC(), 8));

		Mc68k::write8(addr, val);
	}

	void MqMc::dumpMemory(const char* _filename) const
	{
		FILE* hFile = fopen((std::string(_filename) + ".bin").c_str(), "wb");
		fwrite(&m_memory[0], 1, m_memory.size(), hFile);
		fclose(hFile);
	}

	void MqMc::dumpROM(const char* _filename) const
	{
		FILE* hFile = fopen((std::string(_filename) + ".bin").c_str(), "wb");
		fwrite(&m_romRuntimeData[0], 1, m_rom.getSize(), hFile);
		fclose(hFile);
	}

	void MqMc::dumpAssembly(const uint32_t _first, const uint32_t _count) const
	{
		std::stringstream ss;
		ss << "mq_68k_" << _first << '-' << (_first + _count) << ".asm";

		std::ofstream f(ss.str(), std::ios::out);

		for(uint32_t i=_first; i<_first + _count;)
		{
			char disasm[64];
			const auto opSize = disassemble(i, disasm);
			f << HEXN(i,5) << ": " << disasm << std::endl;
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
		ss << "illegalInstruction_" << HEXN(getPC(), 8) << "_op" << HEXN(opcode,8);
		dumpMemory(ss.str().c_str());

		return Mc68k::onIllegalInstruction(opcode);
	}
}
