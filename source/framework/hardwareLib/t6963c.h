#pragma once

#include "baseLib/event.h"

#include <cstdint>
#include <vector>

namespace hwLib
{
	class T6963C
	{
	public:
		T6963C(uint32_t _width, uint32_t _height, std::vector<uint8_t> _cgRom = {});

		void reset();
		// Without a CG ROM image, text codes are drawn from a built-in ASCII font. A board
		// whose module has no external CG ROM uses the controller's internal one instead,
		// which is coded ASCII - 0x20.
		void setInternalCgRom(const bool _internal) { m_internalCgRom = _internal; m_dirty = true; }
		uint8_t read(uint16_t _offset);
		void write(uint16_t _offset, uint8_t _data);
		void renderMono(std::vector<uint8_t>& _output) const;
		void flush();

		uint32_t width() const { return m_width; }
		uint32_t height() const { return m_height; }
		const std::vector<uint8_t>& memory() const { return m_memory; }
		uint16_t textHome() const { return m_textHome; }
		uint16_t textArea() const { return m_textArea; }
		uint16_t graphicHome() const { return m_graphicHome; }
		uint16_t graphicArea() const { return m_graphicArea; }
		uint16_t cgRamOffset() const { return m_cgRamOffset; }
		uint8_t mode() const { return m_mode; }
		uint8_t displayMode() const { return m_displayMode; }
		baseLib::Event<> evChanged;

	private:
		enum class AutoMode : uint8_t { Read, Write, None };
		void command(uint8_t _command);
		void changed();

		uint32_t m_width;
		uint32_t m_height;
		std::vector<uint8_t> m_cgRom;
		std::vector<uint8_t> m_memory = std::vector<uint8_t>(0x8000, 0);
		uint16_t m_data = 0;
		uint16_t m_address = 0;
		uint16_t m_graphicHome = 0;
		uint16_t m_textHome = 0;
		uint16_t m_graphicArea = 0;
		uint16_t m_textArea = 0;
		uint16_t m_cgRamOffset = 0;
		uint8_t m_mode = 0;
		uint8_t m_displayMode = 0;
		uint8_t m_readData = 0;
		uint8_t m_fontSize = 6;
		AutoMode m_autoMode = AutoMode::None;
		bool m_dirty = true;
		bool m_internalCgRom = false;
	};
}
