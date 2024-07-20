#include "n2xmc.h"

#include <cassert>

#include "n2xdsp.h"
#include "n2xrom.h"
#include "synthLib/midiTypes.h"

namespace n2x
{
	// OC2 = PGP4 = SDA
	// OC3 = PGP5 = SCL

	static constexpr uint32_t g_bitSDA = 4;
	static constexpr uint32_t g_bitSCL = 5;

	Microcontroller::Microcontroller(const Rom& _rom) : m_midi(getQSM())
	{
		if(!_rom.isValid())
			return;

		m_rom = _rom.data();
		m_ram.fill(0);

//		dumpAssembly("n2x_68k.asm", g_romAddress, g_romSize);

		reset();

		setPC(g_pcInitial);

		getPortF().writeRX(0xff);

		getPortGP().setWriteTXCallback([this](const mc68k::Port& _port)
		{
			const auto v = _port.read();
			const auto d = _port.getDirection();

			const auto sdaV = (v >> g_bitSDA) & 1;
			const auto sclV = (v >> g_bitSCL) & 1;

			const auto sdaD = (d >> g_bitSDA) & 1;
			const auto sclD = (d >> g_bitSCL) & 1;

			LOG("PortGP write SDA=" << sdaV << " SCL=" << sclV);

			if(sdaD && sclD)
				m_flash.masterWrite(sdaV, sclV);
			else if(!sdaD && sclD)
			{
				if(const auto res = m_flash.masterRead(sclV))
				{
					auto r = v;
					r &= ~(1 << g_bitSDA);
					r |= *res ? g_bitSDA : 0;

					getPortGP().writeRX(r);
					LOG("PortGP return SDA=" << *res);
				}
				else
				{
//					LOG("PortGP return SDA={}, wrong SCL");
				}
			}
		});
		getPortGP().setDirectionChangeCallback([this](const mc68k::Port& _port)
		{
			// OC2 = PGP4 = SDA
			// OC3 = PGP5 = SCL
			const auto p = _port.getDirection();
			const auto sda = (p >> g_bitSDA) & 1;
			const auto scl = (p >> g_bitSCL) & 1;
			LOG("PortGP dir SDA=" << (sda?"w":"r") << " SCL=" << (scl?"w":"r"));
			if(scl)
			{
				const auto ack = m_flash.setSdaWrite(sda);
				if(ack)
				{
					LOG("Write ACK " << (*ack));
					getPortGP().writeRX(*ack ? (1<<g_bitSDA) : 0);
				}
			}
		});
	}

	uint32_t Microcontroller::read32(uint32_t _addr)
	{
		return Mc68k::read32(_addr);
	}

	uint16_t Microcontroller::readImm16(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return readW(m_rom.data(), _addr);
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return readW(m_ram.data(), _addr - g_ramAddress);

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		assert(!m_hdi08A.isInRange(pa));
		assert(!m_hdi08B.isInRange(pa));
		assert(!m_panel.isInRange(pa));

		return 0;
	}

	uint16_t Microcontroller::read16(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return readW(m_rom.data(), _addr);
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return readW(m_ram.data(), _addr - g_ramAddress);

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))										return m_hdi08A.read16(pa);
		if(m_hdi08B.isInRange(pa))										return m_hdi08B.read16(pa);

		if(m_panel.cs4().isInRange(pa))
		{
			LOG("Read Frontpanel CS4 " << HEX(_addr));
			return m_panel.cs4().read16(pa);
		}

		if(m_panel.cs6().isInRange(pa))
		{
			LOG("Read Frontpanel CS6 " << HEX(_addr));
			return m_panel.cs6().read16(pa);
		}

		if(_addr >= g_keyboardAddress && _addr < g_keyboardAddress + g_keyboardSize)
		{
			LOG("Read Keyboard A " << HEX(_addr));
			return 0;
		}

		return Mc68k::read16(_addr);
	}

	uint8_t Microcontroller::read8(const uint32_t _addr)
	{
		if(_addr < g_romSize)											return m_rom[_addr];
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)	return m_ram[_addr - g_ramAddress];

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))										return m_hdi08A.read8(pa);
		if(m_hdi08B.isInRange(pa))										return m_hdi08B.read8(pa);
		
		if(m_panel.cs4().isInRange(pa))
		{
			LOG("Read Frontpanel CS4 " << HEX(_addr));
			return 0xff;//m_panel.cs4().read8(pa);
		}

		if(m_panel.cs6().isInRange(pa))
		{
			LOG("Read Frontpanel CS6 " << HEX(_addr));
			return m_panel.cs6().read8(pa);
		}

		if(_addr >= g_keyboardAddress && _addr < g_keyboardAddress + g_keyboardSize)
		{
			LOG("Read Keyboard A " << HEX(_addr));
			return 0xff;
		}

		return Mc68k::read8(_addr);
	}

	void Microcontroller::write16(const uint32_t _addr, const uint16_t _val)
	{
		if(_addr < g_romSize)
		{
			assert(false);
			writeW(m_rom.data(), _addr, _val);
			return;
		}
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)
		{
			writeW(m_ram.data(), _addr - g_ramAddress, _val);
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))
		{
			m_hdi08A.write16(pa, _val);
			return;
		}

		if(m_hdi08B.isInRange(pa))
		{
			m_hdi08B.write16(pa, _val);
			return;
		}

		if(m_panel.cs4().isInRange(pa))
		{
			LOG("Write Frontpanel CS4 " << HEX(_addr) << "=" << HEXN(_val, 4));
			m_panel.cs4().write16(pa, _val);
			return;
		}

		if(m_panel.cs6().isInRange(pa))
		{
			LOG("Write Frontpanel CS6 " << HEX(_addr) << "=" << HEXN(_val, 4));
			m_panel.cs6().write16(pa, _val);
			return;
		}

		Mc68k::write16(_addr, _val);
	}

	void Microcontroller::write8(const uint32_t _addr, const uint8_t _val)
	{
		if(_addr < g_romSize)
		{
			assert(false);
			m_rom[_addr] = _val;
			return;
		}
		if(_addr >= g_ramAddress && _addr < g_ramAddress + g_ramSize)
		{
			m_ram[_addr - g_ramAddress] = _val;
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))
		{
			m_hdi08A.write8(pa, _val);
			return;
		}

		if(m_hdi08B.isInRange(pa))
		{
			m_hdi08B.write8(pa, _val);
			return;
		}
		
		if(m_panel.cs4().isInRange(pa))
		{
			LOG("Write Frontpanel CS4 " << HEX(_addr) << "=" << HEXN(_val, 2));
			m_panel.cs4().write8(pa, _val);
			return;
		}

		if(m_panel.cs6().isInRange(pa))
		{
			LOG("Write Frontpanel CS6 " << HEX(_addr) << "=" << HEXN(_val, 2));
			m_panel.cs6().write8(pa, _val);
			return;
		}

		Mc68k::write8(_addr, _val);
	}

	uint32_t Microcontroller::exec()
	{
		const auto pc = getPC();
		m_prevPC = pc;
//		LOG("uc PC=" << HEX(pc));
		if(pc >= g_ramAddress)
		{
//			if(pc == 0x1000c8)
//				dumpAssembly("nl2x_68k_ram.asm", g_ramAddress, g_ramSize);
		}

		static volatile bool writeFlash = false;

		if(writeFlash)
		{
			writeFlash = false;
			m_flash.saveAs("flash_runtime.bin");
		}
		const auto cycles = Mc68k::exec();

		m_hdi08A.exec(cycles);
		m_hdi08B.exec(cycles);

		m_totalCycles += cycles;

		if(m_totalCycles > 0x1000000 && !m_hasSentMidi)
		{
			m_hasSentMidi = true;

			m_midi.writeMidi(synthLib::M_NOTEON);
			m_midi.writeMidi(synthLib::Note_C3);
			m_midi.writeMidi(100);
		}
		return cycles;
	}
}