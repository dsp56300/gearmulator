#include "mqmc.h"

#include <array>
#include <fstream>

#include "rom.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	constexpr uint32_t g_romAddress = 0x80000;
	constexpr uint32_t g_pcInitial = 0x80130;

	MqMc::MqMc(ROM& _rom) : m_rom(_rom)
	{
		m_memory.resize(0x40000, 0);
		m_sim.resize(128, 0);

		clock = 0;
		reset();

		setPC(g_pcInitial);
		setPC0(g_pcInitial);

		std::ofstream f("mq_68k.asm", std::ios::out);

		for(uint32_t i=g_pcInitial; i<g_romAddress + m_rom.getSize();)
		{
			char disasm[64];
			disassemble(getPC(), disasm);
			const auto opSize = disassemble(i, disasm);
			f << HEXN(i,5) << ": " << disasm << std::endl;
			if(!opSize)
				++i;
			else
				i += opSize;
		}
		f.close();	
	}

	MqMc::~MqMc()
	{
	}

	void MqMc::exec()
	{
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

		if(getPC() == 0x80cfe)
			int foo=0;
		if(clock == 0x011cc32a)
		{
			FILE* hFile = fopen("dump.bin", "wb");
			fwrite(&m_memory[0], 1, m_memory.size(), hFile);
			fclose(hFile);
		}

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
		execute();
	}

	moira::u16 MqMc::read16(moira::u32 addr)
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

		return Mc68k::read16(addr);
	}

	moira::u8 MqMc::read8(moira::u32 addr)
	{
		if(addr < m_memory.size())
			return m_memory[addr];

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
			return m_rom.getData()[addr - g_romAddress];

		return Mc68k::read8(addr);
	}

	void MqMc::write16(moira::u32 addr, moira::u16 val)
	{
		if(addr < m_memory.size())
		{
			writeW(m_memory, addr, val);
			return;
		}

		LOG("write16 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4));

		Mc68k::write16(addr, val);
	}

	void MqMc::write8(moira::u32 addr, moira::u8 val)
	{
		if(addr < m_memory.size())
		{
			m_memory[addr] = val;
			return;
		}

		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << static_cast<char>(val));

		Mc68k::write8(addr, val);
	}

	void MqMc::signalResetInstr()
	{
		FILE* hFile = fopen("dump_reset.bin", "wb");
		fwrite(&m_memory[0], 1, m_memory.size(), hFile);
		fclose(hFile);
		Mc68k::signalResetInstr();
	}
}
