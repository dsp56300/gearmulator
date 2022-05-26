#pragma once

#include "../68kEmu/mc68k.h"

namespace mqLib
{
	class ROM;

	class MqMc : public mc68k::Mc68k
	{
	public:
		explicit MqMc(ROM& _rom);
		~MqMc() override;

		void exec();

	private:
		moira::u16 read16(moira::u32 addr) override;
		moira::u8 read8(moira::u32 addr) override;
		void write16(moira::u32 addr, moira::u16 val) override;
		void write8(moira::u32 addr, moira::u8 val) override;

		ROM& m_rom;
	};
}
