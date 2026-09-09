#pragma once

#include <cstdint>
#include <vector>

#include "baseLib/event.h"

namespace hwLib
{
	// Epson SED1335 (S1D13305) LCD controller emulation.
	//
	// Sibling of the HD44780 character LCD (lcd.h). The SED1335 is a graphical
	// controller: the host writes commands + parameters through a command port
	// and pixel/text data through a data port, and the controller composites one
	// or more text/graphics layers out of its display RAM.
	//
	// The host side is two byte ports:
	//   writeCommand(cmd)  — command / parameter-select port
	//   writeData(value)   — parameter bytes and MWRITE pixel/character data
	//   readData()         — MREAD (approximate)
	//
	// Panel resolution and the amount of connected display RAM are board wiring
	// details, so they are passed to the constructor. renderMono() rasterises
	// the current display RAM into a 1-bit-per-pixel framebuffer (0 = background,
	// 1 = lit) sized panelWidth x panelHeight; the caller colourises and blits it.
	//
	// TODO(other panels): the following are real SED1335 behaviours that this
	// class currently parses-but-ignores or approximates. Each is safe to skip
	// for the panel driven here; implement before relying on this class for a
	// different panel or
	// firmware (see the referenced call sites for the details):
	//   * Cursor auto-increment direction (CSRDIR) — writeData() / readData().
	//   * Split screen (SL1/SL2) and blocks 3/4, plus the DISP-ON page-enable /
	//     flash attributes — renderMono().
	//   * Per-block text/graphics select (DM1/DM2) and 3-layer OV — OVLAY handler.
	//   * Hardware cursor (CSRFORM size/shape, cursor flash) — writeData().
	//   * MX composition beyond OR/XOR (AND, priority-OR) — renderMono()'s blend.
	//   * CSRR (0x47) cursor-address read-back — readData().
	//
	class SED1335
	{
	public:
		// _vramSize must be a power of two (the controller's address counter is
		// masked to it).
		SED1335(uint32_t _panelWidth, uint32_t _panelHeight, uint32_t _vramSize);
		void reset();

		// Fires (at most once per flush()) after any display-affecting write.
		// Payload-free: listeners hold the SED1335 and call renderMono() to repaint.
		baseLib::Event<> evChanged;

		uint32_t width()  const { return m_width; }
		uint32_t height() const { return m_height; }

		// Host bus ports.
		void    writeCommand(uint8_t _cmd);
		void    writeData(uint8_t _value);
		uint8_t readData();

		// Coalesced change notification — call once per host frame.
		void flush();

		bool isDisplayEnabled() const { return m_displayEnabled; }
		const std::vector<uint8_t>& memory() const { return m_memory; }

		// Raw controller configuration, for host code that needs to interpret
		// VRAM itself (character cell size, line pitch, layer start addresses).
		uint8_t  configFx()   const { return m_configFx; }
		uint8_t  configFy()   const { return m_configFy; }
		uint8_t  configCr()   const { return m_configCr; }
		uint8_t  configTcr()  const { return m_configTcr; }
		uint16_t configAp()   const { return m_configAp; }
		bool     usesExternalCharacterGenerator() const { return m_configM0; }
		uint16_t cgramAddress() const { return m_cgramAdr; }
		uint16_t scrollSad1() const { return m_scrollSad1; }
		uint16_t scrollSad2() const { return m_scrollSad2; }

		// Rasterise the current display into a 1bpp framebuffer (resized to
		// width()*height(), row-major, 0/1 per pixel).
		void renderMono(std::vector<uint8_t>& _out) const;

	private:
		void startParam(uint8_t _cmd, int _bytesExpected);
		uint8_t fontChar(uint16_t _ch, int _row) const;
		uint8_t textChar(uint16_t _base, int _x, int _y, int _cr) const;

		const uint32_t m_width;
		const uint32_t m_height;
		const uint32_t m_memMask;

		// Command sequencing.
		int  m_mode  = 0;
		int  m_stage = 0;
		int  m_dataRemaining = 0;
		bool m_displayEnabled = false;
		bool m_dirty = false;

		// SYSTEM SET config.
		bool    m_configM0 = false;	// internal/external CG ROM
		bool    m_configM1 = false;	// D6 correction
		bool    m_configM2 = false;	// 8/16 px height
		bool    m_configWs = false;	// single/dual panel
		bool    m_configIv = false;	// invert
		uint8_t m_configFx = 0;		// char pixel width - 1
		bool    m_configWf = false;	// AC frame WF period
		uint8_t m_configFy = 0;		// char pixel height - 1
		uint8_t m_configCr = 0;		// bytes per line - 1
		uint8_t m_configTcr = 0;	// total line length
		uint8_t m_configLf = 0;		// height in lines
		uint16_t m_configAp = 0;	// virtual screen horizontal address range (line pitch AP; TODO: renderMono uses CR)

		// SCROLL / display start.
		uint16_t m_scrollSad1 = 0, m_scrollSad2 = 0, m_scrollSad3 = 0, m_scrollSad4 = 0;
		uint8_t  m_scrollSl1 = 0, m_scrollSl2 = 0;
		uint8_t  m_hdotScr = 0;

		// OVLAY composition.
		uint8_t m_mxMode = 0;		// 00=OR 01=XOR 10=AND 11=priority-OR
		bool    m_dm1 = false;		// block 1: 0=text 1=graphics
		bool    m_dm2 = false;		// block 3: 0=text 1=graphics
		bool    m_threeLayer = false;	// OV

		uint16_t m_cgramAdr = 0;
		uint8_t  m_cursorDir = 0;
		uint16_t m_cursor = 0;

		std::vector<uint8_t> m_memory;
	};
}
