#include "mqmc.h"

#include "rom.h"

#include "dsp56kEmu/logging.h"

namespace mqLib
{
	constexpr uint32_t g_romAddress = 0x80000;
	constexpr uint32_t g_pcInitial = 0x80130;

	MqMc::MqMc(ROM& _rom) : m_rom(_rom)
	{
		reset();

		setPC(g_pcInitial);
		setPC0(g_pcInitial);
		debugger.enableLogging();
	}

	MqMc::~MqMc()
	{
	}

	void MqMc::exec()
	{
		char disasm[64];
		disassemble(getPC(), disasm);
		LOG("Exec @" << HEXN(getPC(), 8) << "=" << disasm);
		execute();
	}

	moira::u16 MqMc::read16(moira::u32 addr)
	{
		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			const uint16_t a = m_rom.getData()[addr - g_romAddress];
			const uint16_t b = m_rom.getData()[addr - g_romAddress + 1];
			const auto r = (a<<8) | b;
			LOG("read16 from ROM addr=" << HEXN(addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}
		switch(addr)
		{
		case 0xfffa04:	//  $YFFA04 CLOCK SYNTHESIZER CONTROL (SYNCR)
			return 0xffff;
		}
		LOG("read16 addr=" << HEXN(addr, 8));
		return 0;
	}

	moira::u8 MqMc::read8(moira::u32 addr)
	{
		LOG("read8 addr=" << HEXN(addr, 8));
		if(addr >= g_romAddress && addr < g_romAddress + m_rom.getSize())
		{
			return m_rom.getData()[addr - g_romAddress];
		}
		return 0;
	}

	void MqMc::write16(moira::u32 addr, moira::u16 val)
	{
		LOG("write16 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,4));
	}

	void MqMc::write8(moira::u32 addr, moira::u8 val)
	{
		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2));
	}
}
