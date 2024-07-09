#include "xtUc.h"

#include <cassert>
#include <cstring>

#include "xtRom.h"

#include "../mc68k/logging.h"
#include "dsp56kEmu/utils.h"

namespace xt
{
	XtUc::XtUc(const Rom& _rom)
	: m_flash(m_romRuntimeData.data(), m_romRuntimeData.size(), false, true)
	, m_pic(*this, m_lcd)
	{
		if(!_rom.isValid())
			return;

		memcpy(m_romRuntimeData.data(), _rom.getData(), g_romSize);
		m_memory.fill(0);

//		dumpAssembly("xt_68k.asm", g_romAddr, g_romSize);

		reset();
		setPC(0x100100);

		getPortGP().setWriteTXCallback([this](const mc68k::Port&)
		{
//			onPortGPWritten();
		});

		getPortE().setWriteTXCallback([this](const mc68k::Port&)
		{
//			onPortEWritten();
		});

		getPortF().setWriteTXCallback([this](const mc68k::Port&)
		{
//			onPortFWritten();
		});

		getPortQS().setDirectionChangeCallback([this](const mc68k::Port&)
		{
			onPortQSWritten();
		});

		getPortQS().setWriteTXCallback([this](const mc68k::Port&)
		{
			onPortQSWritten();
		});
	}

	uint32_t XtUc::exec()
	{
//		LOG("PC: " << HEX(getPC()));
		const auto cycles = Mc68k::exec();
		m_hdiA.exec(cycles);
		return cycles;
	}

	uint16_t XtUc::readImm16(const uint32_t _addr)
	{
		const auto addr = _addr & g_addrMask;

		if(addr < g_ramSize)
		{
			return readW(m_memory.data(), addr);
		}

		if(addr >= g_romAddr && addr < g_romAddr + Rom::Size)
		{
			const auto r = readW(m_romRuntimeData.data(), addr - g_romAddr);
//			LOG("read16 from ROM addr=" << HEXN(_addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}
#ifdef _DEBUG
		dsp56k::nativeDebugBreak();
#endif
		return 0;
	}

	uint16_t XtUc::read16(const uint32_t _addr)
	{
		const auto addr = _addr & g_addrMask;

		if(addr < g_ramSize)
		{
			return readW(m_memory.data(), addr);
		}

		if(addr >= g_romAddr && addr < g_romAddr + Rom::Size)
		{
			const auto r = readW(m_romRuntimeData.data(), addr - g_romAddr);
//			LOG("read16 from ROM addr=" << HEXN(_addr, 8) << " val=" << HEXN(r, 4));
			return r;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if (m_hdiA.isInRange(pa))
			return m_hdiA.read16(pa);

//		LOG("read16 addr=" << HEXN(_addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read16(addr);
	}

	uint8_t XtUc::read8(const uint32_t _addr)
	{
		const auto addr = _addr & g_addrMask;

		if(addr < g_ramSize)
			return m_memory[addr];

		if(addr >= g_romAddr && addr < g_romAddr + Rom::Size)
			return m_romRuntimeData[addr - g_romAddr];

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if(m_hdiA.isInRange(pa))
			return m_hdiA.read8(pa);

//		LOG("read8 addr=" << HEXN(addr, 8) << ", pc=" << HEXN(getPC(), 8));

		return Mc68k::read8(addr);
	}

	void XtUc::write16(const uint32_t _addr, uint16_t val)
	{
		const auto addr = _addr & g_addrMask;

		if(addr < g_ramSize)
		{
			writeW(m_memory.data(), addr, val);
			return;
		}

		if(addr >= g_romAddr && addr < g_romAddr + Rom::Size)
		{
			MCLOG("write16 TO ROM addr=" << MCHEXN(addr, 8) << ", value=" << MCHEXN(val,4) << ", pc=" << MCHEXN(getPC(), 8));
			m_flash.write(addr - g_romAddr, val);
			return;
		}

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);

		if (m_hdiA.isInRange(pa))
		{
			m_hdiA.write16(pa, val);
			return;
		}

		Mc68k::write16(addr, val);
	}

	void XtUc::write8(const uint32_t _addr, uint8_t val)
	{
		const auto addr = _addr & g_addrMask;

		if(addr < g_ramSize)
		{
			m_memory[addr] = val;
			return;
		}

		if(addr >= g_romAddr && addr < g_romAddr + Rom::Size)
		{
			MCLOG("write8 TO ROM addr=" << MCHEXN(addr, 8) << ", value=" << MCHEXN(val,2) << ", pc=" << MCHEXN(getPC(), 8));
			m_flash.write(addr - g_romAddr, val);
			return;
		}

//		LOG("write8 addr=" << HEXN(addr, 8) << ", value=" << HEXN(val,2) << ", pc=" << HEXN(getPC(), 8));

		const auto pa = static_cast<mc68k::PeriphAddress>(addr & mc68k::g_peripheralMask);
		if (m_hdiA.isInRange(pa))
		{
			m_hdiA.write8(pa, val);
			return;
		}

		Mc68k::write8(addr, val);
	}

	void XtUc::onPortQSWritten()
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
	}

	void XtUc::setButton(ButtonType _type, const bool _pressed)
	{
		m_pic.setButton(_type, _pressed);
	}

	void XtUc::setLcdDirtyCallback(const Pic::DirtyCallback& _callback)
	{
		m_pic.setLcdDirtyCallback(_callback);
	}

	void XtUc::setLedsDirtyCallback(const Pic::DirtyCallback& _callback)
	{
		m_pic.setLedsDirtyCallback(_callback);
	}

	bool XtUc::getLedState(LedType _led) const
	{
		return m_pic.getLedState(_led);
	}

	bool XtUc::getButton(ButtonType _button) const
	{
		return m_pic.getButton(_button);
	}
}
