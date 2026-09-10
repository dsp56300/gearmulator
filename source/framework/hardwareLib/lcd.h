#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include "hd44780.h"

namespace hwLib
{
	// 2*20 characters display simulation (20*2)
	// EW20290GLW / NHD-0220DZW-AB5 and compatibles
	//
	// This is a view onto Hd44780, not a second emulation of the chip. It exists because its
	// callers - Vavra, JE8086 and the sysex remote control - want the visible 2x20 window as a
	// flat array<char, 40>, which is also the wire format sendSysexLcdDdRam() ships. Hd44780
	// keeps all 80 DDRAM cells and applies the window on the way out, so that array is rebuilt
	// from it whenever the content changes.
	class LCD
	{
	public:
		using ChangeCallback = std::function<void()>;

		LCD();

		std::optional<uint8_t> exec(const bool _registerSelect, const bool _read, const uint8_t _data)
		{
			return m_hd.exec(_registerSelect, _read, _data);
		}

		const std::array<char, 40>& getDdRam() const { return m_ddRam; }
		const auto& getCgRam() const { return m_hd.getCgRam(); }
		bool getCgData(std::array<uint8_t, 8>& _data, const uint32_t _charIndex) const
		{
			return m_hd.getCgData(_data, _charIndex);
		}

		void setChangeCallback(const ChangeCallback& _callback)
		{
			m_changeCallback = _callback;
		}

		void setCgRamChangeCallback(const ChangeCallback& _callback)
		{
			m_hd.setCgRamChangeCallback(_callback);
		}

	private:
		Hd44780 m_hd{20, 2};

		// The visible window, kept in step with m_hd so getDdRam() can hand out a reference.
		std::array<char, 40> m_ddRam{};

		ChangeCallback m_changeCallback;
	};
}
