#include "mqmc.h"

#include <array>
#include <fstream>

#include "rom.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	constexpr uint32_t g_romAddress = 0x80000;
	constexpr uint32_t g_pcInitial = 0x80130;

	static std::string logChar(char _val)
	{
		std::stringstream ss;
		if(_val > 32 && _val < 127)
			ss << _val;
		else
			ss << "[" << HEXN(static_cast<uint8_t>(_val), 2) << "]";
		return ss.str();
	}

	MqMc::MqMc(ROM& _rom) : m_rom(_rom)
	{
		m_memory.resize(0x40000, 0);

		// TODO code tests bit 5 and doesn't want to continue if that input is zero
//		getPortF().writeRX((1<<5));

		reset();

		setPC(g_pcInitial);

#if 0
		dumpAssembly(g_romAddress, g_romSize);
#endif
	}

	MqMc::~MqMc()
	{
	}

	void MqMc::exec()
	{
#if 0
		for(auto it = m_lastPCs.begin(); it != m_lastPCs.end(); ++it)
		{
			if(*it == getPC())
			{
				m_lastPCs.erase(it);
				break;
			}
		}

		m_lastPCs.push_back(getPC());
		if(m_lastPCs.size() > 32)
			m_lastPCs.pop_front();
#endif
		if(getPC() == 0x80718)
		{
			m_memory[0x170] = 32;
			dumpMemory("spinloop");
		}
#if 0
		if(clock == 0x011cc32a)
		{
			FILE* hFile = fopen("dump.bin", "wb");
			fwrite(&m_memory[0], 1, m_memory.size(), hFile);
			fclose(hFile);
		}
#endif
		if(getPC() == 0x80228)
		{
			int foo=0;
		}
#if 0
		if(clock > 0x14c704c)
		{
			char disasm[64];
			disassemble(getPC(), disasm);
			LOG("Exec @ " << HEXN(getPC(), 8) << "  = " << disasm << " (clock " << HEXN(clock, 8) << ')');
		}
#endif

		Mc68k::exec();
	}

	uint16_t MqMc::read16(uint32_t addr)
	{
		if(addr < m_memory.size())
		{
			return readW(m_memory, addr);
		}

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			const auto r = readW(m_rom.getData(), addr - g_romAddress);
//			LOG("read16 from ROM addr=" << HEXN(addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}
		
		LOG("read16 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read16(addr);
	}

	uint8_t MqMc::read8(uint32_t addr)
	{
		if(addr < m_memory.size())
			return m_memory[addr];

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
			return m_rom.getData()[addr - g_romAddress];

		LOG("read8 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read8(addr);
	}

	void MqMc::write16(uint32_t addr, uint16_t val)
	{
		if(addr >= 0x814 && addr < 0x814 + 0x50)
		{
			LOG("LCD WRITE16 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4) << ", pc=" << HEXN(getPC(), 8));
		}

		if(addr < m_memory.size())
		{
			writeW(m_memory, addr, val);
			return;
		}

		LOG("write16 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4) << ", pc=" << HEXN(getPC(), 8));

		Mc68k::write16(addr, val);
	}

	void MqMc::write8(uint32_t addr, uint8_t val)
	{
		if(addr >= 0xffffd000)
			int foo=0;

		if(addr >= 0x814 && addr < 0x814 + 0x50)
		{
			LOG("LCD WRITE8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << logChar(val) << ", pc=" << HEXN(getPC(), 8));
		}

		if(addr < m_memory.size())
		{
			m_memory[addr] = val;
			return;
		}

		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << logChar(val) << ", pc=" << HEXN(getPC(), 8));

		Mc68k::write8(addr, val);
	}

	void MqMc::dumpMemory(const char* _filename) const
	{
		FILE* hFile = fopen((std::string(_filename) + ".bin").c_str(), "wb");
		fwrite(&m_memory[0], 1, m_memory.size(), hFile);
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
}
