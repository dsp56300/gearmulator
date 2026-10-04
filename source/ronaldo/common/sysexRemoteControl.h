#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "synthLib/midiTypes.h"

namespace hwLib
{
	class Hd44780;
}

namespace rLib
{
	// Editor <-> device transport of the Roland plugins: F0 7D [prefix] <command> <payload> F7. Each device
	// keeps its own command numbers (some messages end up in saved plugin state); this class holds the
	// framing and the shared payload codecs.
	//
	// The LCD payloads carry raw DDRAM/CGRAM bytes and are not 7-bit clean, so these messages must never be
	// forwarded to a real MIDI port.
	class SysexRemoteControl
	{
	public:
		static constexpr uint8_t ManufacturerId = 0x7d;
		static constexpr uint32_t CgRamSize = 64;
		static constexpr uint32_t DdRamSize = 80;
		static constexpr uint32_t VisibleDdRamSize = 40;	// the 2 x 20 window the editors render

		struct LcdCursorState
		{
			bool displayOn;
			bool cursorOn;
			bool cursorBlinking;
			uint8_t address;	// raw HD44780 DDRAM address (0..0x7f)
		};

		// _prefix: bytes between the manufacturer id and the command byte
		explicit SysexRemoteControl(std::initializer_list<uint8_t> _prefix = {});
		virtual ~SysexRemoteControl() = default;

		// True if this was one of our messages and it was dispatched.
		bool receive(const synthLib::SMidiEvent& _input) const;
		bool receive(const synthLib::SysexBuffer& _input) const;

	protected:
		virtual bool onCommand(uint8_t _command, const uint8_t* _payload, size_t _size) const = 0;

		// Appends an event holding F0 7D <_header...>; the caller adds the payload and end().
		static synthLib::SysexBuffer& begin(std::vector<synthLib::SMidiEvent>& _dst, std::initializer_list<uint8_t> _header);
		template<typename T> static synthLib::SysexBuffer& begin(std::vector<synthLib::SMidiEvent>& _dst, const T _command)
		{
			return begin(_dst, {static_cast<uint8_t>(_command)});
		}
		static void end(synthLib::SysexBuffer& _sysex) { _sysex.push_back(0xf7); }

		template<typename T> static void sendCommand(std::vector<synthLib::SMidiEvent>& _dst, const T _command)
		{
			end(begin(_dst, _command));
		}

		// _count nibbles of _value, most significant first
		static void pushNibbles(synthLib::SysexBuffer& _dst, uint64_t _value, uint32_t _count);
		static uint64_t readNibbles(const uint8_t* _src, uint32_t _count);

		static void pushLcdCgRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd);
		static void pushLcdDdRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd);
		static void pushLcdVisibleDdRam(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd);
		static void pushLcdCursor(synthLib::SysexBuffer& _dst, const hwLib::Hd44780& _lcd);
		static LcdCursorState readLcdCursor(const uint8_t* _src);

		// Graphic LCD: width and height as 3 nibbles each, then the 1-byte-per-pixel _mono packed to 1bpp,
		// each packed byte as two nibbles, low first.
		static void pushLcdVram(synthLib::SysexBuffer& _dst, uint16_t _width, uint16_t _height, const std::vector<uint8_t>& _mono);
		// Unpacks into a scratch buffer reused across frames, so a repaint does not reallocate the framebuffer.
		const std::vector<uint8_t>* readLcdVram(const uint8_t* _src, size_t _size, uint16_t& _width, uint16_t& _height) const;

		template<typename T, size_t N> static std::array<T, N> readArray(const uint8_t* _src)
		{
			std::array<T, N> a{};
			for(size_t i = 0; i < N; ++i)
				a[i] = static_cast<T>(_src[i]);
			return a;
		}

	private:
		std::vector<uint8_t> m_prefix;
		mutable std::vector<uint8_t> m_lcdScratch;
	};
}
