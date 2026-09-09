#include "sed1335.h"

#include "lcdfonts.h"

#include <algorithm>

namespace hwLib
{
	SED1335::SED1335(const uint32_t _panelWidth, const uint32_t _panelHeight, const uint32_t _vramSize)
		: m_width(_panelWidth)
		, m_height(_panelHeight)
		, m_memMask(_vramSize - 1)
		, m_memory(_vramSize, 0)
	{
		reset();
	}

	void SED1335::reset()
	{
		m_mode = 0;
		m_stage = 0;
		m_dataRemaining = 0;
		m_displayEnabled = false;
		m_dirty = false;
		m_configM0 = false;
		m_configM1 = false;
		m_configM2 = false;
		m_configWs = false;
		m_configIv = false;
		m_configFx = 0;
		m_configWf = false;
		m_configFy = 0;
		m_configCr = 0;
		m_configTcr = 0;
		m_configLf = 0;
		m_configAp = 0;
		m_scrollSad1 = m_scrollSad2 = m_scrollSad3 = m_scrollSad4 = 0;
		m_scrollSl1 = m_scrollSl2 = 0;
		m_hdotScr = 0;
		m_mxMode = 0;
		m_dm1 = false;
		m_dm2 = false;
		m_threeLayer = false;
		m_cgramAdr = 0;
		m_cursorDir = 0;
		m_cursor = 0;
		std::fill(m_memory.begin(), m_memory.end(), 0);
	}

	void SED1335::startParam(const uint8_t _cmd, const int _bytesExpected)
	{
		m_mode = _cmd;
		m_stage = 0;
		m_dataRemaining = _bytesExpected;
	}

	void SED1335::writeCommand(const uint8_t _cmd)
	{
		switch (_cmd)
		{
		case 0x40:	// SYSTEM SET (8 bytes)
			m_displayEnabled = false;
			startParam(0x40, 8);
			return;
		case 0x42:	// MWRITE (open-ended)
			m_mode = 0x42;
			m_dataRemaining = 0;
			return;
		case 0x43:	// MREAD (open-ended)
			m_mode = 0x43;
			m_dataRemaining = 0;
			return;
		case 0x44:	// SCROLL (10 bytes)
			startParam(0x44, 10);
			return;
		case 0x46:	// CSRW (2 bytes)
			startParam(0x46, 2);
			return;
		case 0x47:	// CSRR (2 bytes)
			startParam(0x47, 2);
			return;
		case 0x4c: case 0x4d: case 0x4e: case 0x4f:	// CSRDIR (no data)
			m_cursorDir = _cmd & 3;
			m_mode = 0;
			return;
		case 0x58:	// DISP OFF (1 byte cursor-blink param)
			m_displayEnabled = false;
			startParam(0x58, 1);
			return;
		case 0x59:	// DISP ON (1 byte cursor-blink param)
			m_displayEnabled = true;
			m_dirty = true;
			startParam(0x59, 1);
			return;
		case 0x5a:	// HDOT SCR (1 byte)
			startParam(0x5a, 1);
			return;
		case 0x5b:	// OVLAY (1 byte)
			startParam(0x5b, 1);
			return;
		case 0x5c:	// CGRAM ADR (2 bytes)
			startParam(0x5c, 2);
			return;
		case 0x5d:	// CSRFORM (2 bytes)
			startParam(0x5d, 2);
			return;
		default:
			// Unknown command — ignore (keeps a bad byte from wedging a handler).
			m_mode = 0;
			return;
		}
	}

	void SED1335::writeData(const uint8_t _value)
	{
		if (m_mode == 0x40)	// SYSTEM SET params
		{
			switch (m_stage)
			{
			case 0:
				m_configM0 = (_value & 1) != 0;
				m_configM1 = (_value & 2) != 0;
				m_configM2 = (_value & 4) != 0;
				m_configWs = (_value & 8) != 0;
				m_configIv = (_value & 32) != 0;
				break;
			case 1:
				m_configFx = _value & 0x7;
				m_configWf = (_value & 128) != 0;
				break;
			case 2: m_configFy = _value & 0x1f; break;	// TODO: datasheet FY is 4-bit (& 0x0f); 0x1f harmless for the current caller
			case 3: m_configCr = _value; break;
			case 4: m_configTcr = _value; break;
			case 5: m_configLf = _value; break;
			case 6: m_configAp = (m_configAp & 0xff00) | _value; break;
			case 7: m_configAp = (m_configAp & 0x00ff) | (_value << 8); break;
			default: break;
			}
		}
		else if (m_mode == 0x44)	// SCROLL params
		{
			switch (m_stage)
			{
			case 0: m_scrollSad1 = (m_scrollSad1 & 0xff00) | _value; break;
			case 1: m_scrollSad1 = (m_scrollSad1 & 0x00ff) | (_value << 8); break;
			case 2: m_scrollSl1 = _value; break;
			case 3: m_scrollSad2 = (m_scrollSad2 & 0xff00) | _value; break;
			case 4: m_scrollSad2 = (m_scrollSad2 & 0x00ff) | (_value << 8); break;
			case 5: m_scrollSl2 = _value; break;
			case 6: m_scrollSad3 = (m_scrollSad3 & 0xff00) | _value; break;
			case 7: m_scrollSad3 = (m_scrollSad3 & 0x00ff) | (_value << 8); break;
			case 8: m_scrollSad4 = (m_scrollSad4 & 0xff00) | _value; break;
			case 9: m_scrollSad4 = (m_scrollSad4 & 0x00ff) | (_value << 8); break;
			default: break;
			}
		}
		else if (m_mode == 0x46)	// CSRW
		{
			if (m_stage == 0) m_cursor = (m_cursor & 0xff00) | _value;
			else              m_cursor = (m_cursor & 0x00ff) | (_value << 8);
		}
		else if (m_mode == 0x42)	// MWRITE — open-ended, auto-increment
		{
			m_memory[m_cursor & m_memMask] = _value;
			// TODO: honour the CSRDIR direction (m_cursorDir) here instead of a
			//       hard +1. Left/up/down — and the AP-sized vertical step MAME's
			//       increment_csr() applies — are unimplemented. Safe for the
			//       current caller, which only ever streams sequential right-
			//       direction runs.
			m_cursor += 1;
			m_dirty = true;
			return;	// don't decrement m_dataRemaining
		}
		else if (m_mode == 0x5a)	// HDOT SCR
		{
			m_hdotScr = _value & 0x7;
		}
		else if (m_mode == 0x5b)	// OVLAY
		{
			m_mxMode = _value & 3;
			m_dm1 = (_value & 4) != 0;
			m_dm2 = (_value & 8) != 0;
			m_threeLayer = (_value & 16) != 0;
			// TODO: m_dm1/m_dm2 (per-block text vs graphics) and m_threeLayer (OV,
			//       3-layer composition) are stored but ignored — renderMono() hard-
			//       codes block1=text, block2=graphics, 2 layers, which is what
			//       the current caller drives.
		}
		else if (m_mode == 0x5c)	// CGRAM ADR
		{
			if (m_stage == 0) m_cgramAdr = (m_cgramAdr & 0xff00) | _value;
			else              m_cgramAdr = (m_cgramAdr & 0x00ff) | (_value << 8);
		}
		else if (m_mode == 0x58 || m_mode == 0x59 || m_mode == 0x5d)
		{
			// DISP ON/OFF cursor-blink param, CSRFORM — consumed, not modelled.
			// TODO: no hardware cursor. CSRFORM (0x5d) cursor size/shape (block vs
			//       underscore) and the DISP-ON/OFF cursor/page flash-rate fields
			//       are dropped, so renderMono() draws no cursor and no flashing.
			//       The current caller does its own cursor / reverse-video marking
			//       in software.
		}
		else
		{
			// No command is expecting data — drop silently.
			return;
		}

		m_stage += 1;
		if (m_dataRemaining > 0 && --m_dataRemaining == 0)
			m_mode = 0;
	}

	uint8_t SED1335::readData()
	{
		// MREAD auto-increment (approximate — the host rarely reads back).
		// TODO: incomplete read side. (1) CSRR (0x47) should return the cursor
		//       address low/high bytes on successive reads; we return VRAM at the
		//       cursor instead. (2) MREAD ignores m_cursorDir (always +1). Neither
		//       is exercised here (the host drives writes, not read-back).
		const auto v = m_memory[m_cursor & m_memMask];
		if (m_mode == 0x43)
			m_cursor += 1;
		return v;
	}

	void SED1335::flush()
	{
		if (!m_dirty)
			return;
		m_dirty = false;
		evChanged();
	}

	uint8_t SED1335::fontChar(const uint16_t _ch, const int _row) const
	{
		// M0=0 selects the controller's internal character generator. The shared
		// LCD font is stored as five low bits per row; align it to D7..D3 as the
		// SED1335 shifts a character cell out from its most-significant bit.
		if(!m_configM0)
		{
			if(_row < 0 || _row >= 10)
				return 0;
			return static_cast<uint8_t>(getCharacterData(static_cast<uint8_t>(_ch))[_row] << 3);
		}
		const int fh = m_configFy + 1;	// character height in rows
		return m_memory[(m_cgramAdr + _ch * fh + _row) & m_memMask];
	}

	uint8_t SED1335::textChar(const uint16_t _base, const int _x, const int _y, const int _cr) const
	{
		return m_memory[(_base + _y * _cr + _x) & m_memMask];
	}

	void SED1335::renderMono(std::vector<uint8_t>& _out) const
	{
		_out.assign(static_cast<size_t>(m_width) * m_height, 0);

		const int fw = m_configFx + 1;	// char pixel width
		const int fh = m_configFy + 1;	// char pixel height
		const int cr = m_configCr + 1;	// chars-per-line pitch
		if (fw < 1 || fh < 1 || cr < 1)
			return;

		// 2-layer composition (the common OV=0, DM1=0 case):
		//   L1 = block 1 at SAD1 — text (via CGRAM font)
		//   L2 = block 2 at SAD2 — graphics
		//
		// TODO: this renderer is the minimum the current caller needs. Before
		//       reusing it on
		//       another SED1335 panel, add:
		//         * Split screen / extra pages — SL1/SL2 line splits and blocks 3
		//           and 4 (m_scrollSad3/4, m_scrollSl1/2) plus the DISP-ON page-
		//           enable/flash attributes are ignored; only SAD1+SAD2 are drawn.
		//         * Per-block mode & 3rd layer — see OVLAY (m_dm1/m_dm2/
		//           m_threeLayer); block roles and layer count are hard-coded here.
		//         * Hardware cursor — see the CSRFORM / DISP handling in writeData().
		const auto blend = [this](const uint8_t _a, const uint8_t _b) -> uint8_t
		{
			// TODO: OR/XOR are exact; AND is plausible but untested vs hardware;
			//       priority-OR (mx=3) is a rough "layer 1 only" fallback, not the
			//       chip's real priority rule. The current caller uses OR/XOR only.
			switch (m_mxMode)
			{
			case 0:  return _a | _b;	// OR
			case 1:  return _a ^ _b;	// XOR
			case 2:  return _a & _b;	// AND
			default: return _a;			// priority-OR fallback
			}
		};

		for (int py = 0; py < int(m_height); ++py)
		{
			const int cy = py / fh;
			const int fy = py % fh;
			for (int px = 0; px < int(m_width); ++px)
			{
				// Layer 1 (text)
				const int cxText = px / fw;
				const int fxText = px % fw;
				const uint8_t ch1 = textChar(m_scrollSad1, cxText, cy, cr);
				const uint8_t g1  = fontChar(ch1, fy);
				const uint8_t l1  = (g1 & (1 << (7 - fxText))) ? 1 : 0;

				// Layer 2 (graphics). The SED1335 shifts FX dots from each display
				// memory byte, starting at D7; the remaining low bits are not shown.
				// SC-8850 programs FX=6 and AP=27, giving 162 addressable dots per
				// row. Treating a byte as eight dots produces a seam every six pixels.
				const int byteInRow = px / fw;
				const int bitInByte = px % fw;
				const int pitch = m_configAp ? m_configAp : cr;
				const uint16_t addr = static_cast<uint16_t>(m_scrollSad2 + py * pitch + byteInRow);
				const uint8_t gb = m_memory[addr & m_memMask];
				const uint8_t l2 = (gb & (1 << (7 - bitInByte))) ? 1 : 0;

				_out[py * m_width + px] = blend(l1, l2) ? 1 : 0;
			}
		}
	}
}
