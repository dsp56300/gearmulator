#include "n2xmc.h"

#include <cassert>

#include "n2xdsp.h"
#include "n2xrom.h"

#include "baseLib/filesystem.h"

namespace n2x
{
	// OC2 = PGP4 = SDA
	// OC3 = PGP5 = SCL
	// OC5 = PGP7 = sustain pedal

	static constexpr uint32_t g_bitResetDSP = 3;
	static constexpr uint32_t g_bitSDA = 4;
	static constexpr uint32_t g_bitSCL = 5;
	static constexpr uint32_t g_bitResetDAC = 6;

	static constexpr uint32_t g_maskResetDSP = 1 << g_bitResetDSP;
	static constexpr uint32_t g_maskResetDAC = 1 << g_bitResetDAC;

	Microcontroller::Microcontroller(Hardware& _hardware, const Rom& _rom)
	: m_flash(_hardware)
	, m_hdi08(m_hdi08A, m_hdi08B)
	, m_panel(_hardware)
	, m_midi(getQSM(), g_samplerate)
	{
		if(!_rom.isValid())
			return;

		m_romRam.fill(0);
		std::copy(std::begin(_rom.data()), std::end(_rom.data()), std::begin(m_romRam));

		m_midi.setSysexDelay(0.0f, 1);

//		dumpAssembly("n2x_68k.asm", g_romAddress, g_romSize);

		reset();

		setPC(g_pcInitial);

		// keyboard irqs, we do not really care but as the UC spinlooped once while waiting for it to become high we set it to high all the time
		getPortF().writeRX(0xff);

		getPortGP().setWriteTXCallback([this](const mc68k::Port& _port)
		{
			const auto v = _port.read();
			const auto d = _port.getDirection();

			const auto sdaV = (v >> g_bitSDA) & 1;
			const auto sclV = (v >> g_bitSCL) & 1;

			const auto sdaD = (d >> g_bitSDA) & 1;
			const auto sclD = (d >> g_bitSCL) & 1;

//			LOG("PortGP write SDA=" << sdaV << " SCL=" << sclV);

			if(d & g_maskResetDAC)
			{
				if(v & g_maskResetDAC)
				{
				}
				else
				{
					LOG("Reset DAC");
				}
			}
			if((d & g_maskResetDSP))
			{
				if(v & g_maskResetDSP)
				{
				}
				else
				{
					LOG("Reset DSP");
				}
			}

			if(sdaD && sclD)
				m_flash.masterWrite(sdaV, sclV);
			else if(!sdaD && sclD)
			{
				if(const auto res = m_flash.masterRead(sclV))
				{
					auto r = v;
					r &= ~(1 << g_bitSDA);
					r |= *res ? (1<<g_bitSDA) : 0;

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
		return readW(m_romRam.data(), _addr & (m_romRam.size()-1));
	}

	uint16_t Microcontroller::read16(const uint32_t _addr)
	{
		if(_addr < m_romRam.size())
		{
			const auto r = readW(m_romRam.data(), _addr);
			return r;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))	return m_hdi08A.read16(pa);
		if(m_hdi08B.isInRange(pa))	return m_hdi08B.read16(pa);
		if(m_hdi08.isInRange(pa))	return m_hdi08.read16(pa);

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

#ifdef _DEBUG
		if(_addr >= g_keyboardAddress && _addr < g_keyboardAddress + g_keyboardSize)
		{
			assert(false && "keyboard access is unexpected");
			LOG("Read Keyboard " << HEX(_addr));
			return 0;
		}
#endif
		return Mc68k::read16(_addr);
	}

	uint8_t Microcontroller::read8(const uint32_t _addr)
	{
		if(_addr < m_romRam.size())
		{
			const auto r = m_romRam[_addr];
//			LOG("read " << HEX(_addr) << "=" << HEXN(r,2));
			return r;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(_addr);

		if(m_hdi08A.isInRange(pa))	return m_hdi08A.read8(pa);
		if(m_hdi08B.isInRange(pa))	return m_hdi08B.read8(pa);
		if(m_hdi08.isInRange(pa))	return m_hdi08.read8(pa);
		
		if(m_panel.cs4().isInRange(pa))
		{
//			LOG("Read Frontpanel CS4 " << HEX(_addr));
			return m_panel.cs4().read8(pa);
		}

		if(m_panel.cs6().isInRange(pa))
		{
//			LOG("Read Frontpanel CS6 " << HEX(_addr));
			return m_panel.cs6().read8(pa);
		}

#ifdef _DEBUG
		if(_addr >= g_keyboardAddress && _addr < g_keyboardAddress + g_keyboardSize)
		{
			assert(false && "keyboard access is unexpected");
			LOG("Read Keyboard " << HEX(_addr));
			return 0;
		}
#endif
		return Mc68k::read8(_addr);
	}

	void Microcontroller::write16(const uint32_t _addr, const uint16_t _val)
	{
		if(_addr < m_romRam.size())
		{
			assert(_addr >= g_ramAddress);
			writeW(m_romRam.data(), _addr, _val);
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

		if(m_hdi08.isInRange(pa))
		{
			m_hdi08.write16(pa, _val);
			return;
		}

		if(m_panel.cs4().isInRange(pa))
		{
//			LOG("Write Frontpanel CS4 " << HEX(_addr) << "=" << HEXN(_val, 4));
			m_panel.cs4().write16(pa, _val);
			return;
		}

		if(m_panel.cs6().isInRange(pa))
		{
//			LOG("Write Frontpanel CS6 " << HEX(_addr) << "=" << HEXN(_val, 4));
			m_panel.cs6().write16(pa, _val);
			return;
		}

		Mc68k::write16(_addr, _val);
	}

	void Microcontroller::write8(const uint32_t _addr, const uint8_t _val)
	{
		if(_addr < m_romRam.size())
		{
			assert(_addr >= g_ramAddress);
			m_romRam[_addr] = _val;
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
		
		if(m_hdi08.isInRange(pa))
		{
			m_hdi08.write8(pa, _val);
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
//			LOG("Write Frontpanel CS6 " << HEX(_addr) << "=" << HEXN(_val, 2));
			m_panel.cs6().write8(pa, _val);
			return;
		}

		Mc68k::write8(_addr, _val);
	}

	uint32_t Microcontroller::exec()
	{
#ifdef _DEBUG
		const auto pc = getPC();
		m_prevPC = pc;

//		LOG("uc PC=" << HEX(pc));
		if(pc >= g_ramAddress)
		{
//			if(pc == 0x1000c8)
//				dumpAssembly("nl2x_68k_ram.asm", g_ramAddress, g_ramSize);
		}

		static volatile bool writeFlash = false;
		static volatile bool writeRam = false;

		if(writeFlash)
		{
			writeFlash = false;
			m_flash.saveAs("flash_runtime.bin");
		}

		if(writeRam)
		{
			writeRam = false;
			baseLib::filesystem::writeFile("romRam_runtime.bin", m_romRam);
		}
#endif
		const auto cycles = Mc68k::exec();

//		m_hdi08A.exec(cycles);
//		m_hdi08B.exec(cycles);

		return cycles;
	}
}
