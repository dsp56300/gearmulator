#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace mc68k
{
	class Port;
}

namespace mqLib
{
	class LCD
	{
	public:
		using ChangeCallback = std::function<void()>;

		LCD();
		bool exec(mc68k::Port& _portGp, mc68k::Port& _portF);

		const std::array<char, 40>& getDdRam() const { return m_dramData; }
		const auto& getCgRam() const { return m_cgramData; }
		bool getCgData(std::array<uint8_t, 8>& _data, uint32_t _charIndex) const;

		void setChangeCallback(const ChangeCallback& _callback)
		{
			m_changeCallback = _callback;
		}
	private:
		enum class CursorShiftMode
		{
			CursorLeft,
			CursorRight,
			DisplayLeft,
			DisplayRight
		};
		enum class DisplayShiftMode
		{
			Right,
			Left
		};
		enum class FontTable
		{
			EnglishJapanese,
			WesternEuropean1,
			EnglishRussian,
			WesternEuropean2,
		};
		enum class DataLength
		{
			Bit8,
			Bit4
		};

		enum class AddressMode
		{
			DDRam,
			CGRam,
		};

		uint32_t m_lastWriteCounter = 0xffffffff;

		uint32_t m_cursorPos = 0;
		uint32_t m_dramAddr = 0;
		uint32_t m_cgramAddr = 0;

		CursorShiftMode m_cursorShift = CursorShiftMode::CursorLeft;
		DisplayShiftMode m_displayShift = DisplayShiftMode::Left;
		FontTable m_fontTable = FontTable::EnglishJapanese;
		DataLength m_dataLength = DataLength::Bit8;
		AddressMode m_addressMode = AddressMode::DDRam;

		bool m_displayOn = true;
		bool m_cursorOn = false;
		bool m_cursorBlinking = false;

		int m_addrIncrement = 1;

		std::array<uint8_t, 0x40> m_cgramData{};
		std::array<char, 40> m_dramData{};
		uint32_t m_lastOpState = 0;

		ChangeCallback m_changeCallback;
	};
}
