#include "mqmc.h"

#include <array>
#include <fstream>

#include "rom.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	constexpr uint32_t g_romAddress = 0x80000;
	constexpr uint32_t g_pcInitial = 0x80130;
	constexpr uint32_t g_simBase = 0x00fffa00;

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
			f << HEXN(i,5) << ": " << disasm << std::endl   ;
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
		if(clock == 0x011cc32a)
		{
			FILE* hFile = fopen("dump.bin", "wb");
			fwrite(&m_memory[0], 1, m_memory.size(), hFile);
			fclose(hFile);
		}

#if 0
		if(clock > 0xf11cc32a)
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

		LOG("read16 addr=" << HEXN(addr, 8));

		switch(addr)
		{
		case 0xfffa04:	//  $YFFA04 CLOCK SYNTHESIZER CONTROL (SYNCR)
			{
				auto v = readW(m_sim, addr - g_simBase);
				v |= (1<<3);	// code waits until frequency has locked in
				return v;
			}
		case 0xfffa11:
			break;
		}

		if(addr >= g_simBase && addr < g_simBase + m_sim.size())
			readW(m_sim, addr - g_simBase);

		return 0;
	}

	moira::u8 MqMc::read8(moira::u32 addr)
	{
		if(addr < m_memory.size())
			return m_memory[addr];

		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
			return m_rom.getData()[addr - g_romAddress];

		LOG("read8 addr=" << HEXN(addr, 8));

		switch(addr)
		{
			case 0xfffa11:
				m_sim[addr - g_simBase] |= (1<<5);	// code tests bit 5, seems to be an input
				break;
		}
		if(addr >= g_simBase && addr < g_simBase + m_sim.size())
			return m_sim[addr - g_simBase];

		return 0;
	}

	void MqMc::write16(moira::u32 addr, moira::u16 val)
	{
		if(addr < m_memory.size())
		{
			writeW(m_memory, addr, val);
			return;
		}

		LOG("write16 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4));

		if(addr >= g_simBase && addr < g_simBase + m_sim.size())
			writeW(m_sim, addr - g_simBase, val);
	}

	void MqMc::write8(moira::u32 addr, moira::u8 val)
	{
		if(addr < m_memory.size())
		{
			m_memory[addr] = val;
			return;
		}

		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << " char=" << static_cast<char>(val));

		if(addr >= g_simBase && addr < g_simBase + m_sim.size())
			m_sim[addr - g_simBase] = val;
	}
}
