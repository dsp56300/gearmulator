#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

#include "midiTypes.h"

#include "baseLib/event.h"

namespace synthLib
{
	// Emulator-only messages between a device and its editor: the front panel (LCD, LEDs, buttons, encoders) and
	// parameters that exist only in the emulation. One format for every product:
	//
	//   F0 7D 54 75 53 <type> <payload> F7
	//
	// 7D is the MMA id for non-commercial use, 54 75 53 is "TuS". The payload is 7-bit clean and its length follows
	// from the type and its own fields. Types are append-only, a decoder ignores the ones it does not know. Device ->
	// editor frames have source Internal, editor -> device frames source Editor. Never use F0 60: MidiTranslator
	// claims it
	namespace fp
	{
		enum class Type : uint8_t
		{
			LcdText			= 0x01,	// cols, rows, then cols x rows cells packed with pack7. Cells 8-15 are the CGRAM characters
			LcdCgRam		= 0x02,	// 64 bytes: 8 characters of 8 rows, 5 bits each
			LedMask			= 0x03,	// count, then (count + 6) / 7 bytes: LED i is bit i % 7 of byte i / 7
			Button			= 0x04,	// index as 4 nibbles, pressed 0 or 1
			Encoder			= 0x05,	// index, delta + 64
			Param			= 0x06,	// page as 2 nibbles, index as 2 nibbles, int32 value as 8 nibbles
			RequestState	= 0x07,	// mask, 0 = everything: the device answers with its LCD, CGRAM and LEDs
			LcdBitmap		= 0x0a,	// a graphic LCD: width and height as 4 nibbles each, then the rows packed with pack7.
									// A row has bitmapStride(width) bytes, pixel x is bit 7 - x % 8 of byte x / 8, 1 = on

			// reserved, implemented by the first product that needs them
			Pot				= 0x08,	// index, value hi7, value lo7
			LcdCursor		= 0x09,
			Buttons			= 0x0b,	// all buttons as a bitmap
			LedLevels		= 0x0c,

			// 0x01 - 0x5f belong to the framework, 0x60 - 0x7e to the product, see FrontPanelTransport::evProduct
			ProductFirst	= 0x60,
			ProductLast		= 0x7e
		};

		static constexpr uint8_t g_header[] = {0xf0, 0x7d, 0x54, 0x75, 0x53};
		static constexpr size_t g_headerSize = std::size(g_header);
		static constexpr size_t g_typeIndex = g_headerSize;
		static constexpr size_t g_cgRamSize = 64;

		bool isFrontPanel(const SysexBuffer& _sysex);

		// 7 bytes become 8: their low 7 bits, then a byte with their top bits, bit k for byte k. The last group is padded
		void pack7(SysexBuffer& _dst, const uint8_t* _src, size_t _count);
		size_t packedSize(size_t _count);
		// _count bytes from exactly packedSize(_count) bytes at _src
		void unpack7(std::vector<uint8_t>& _dst, const uint8_t* _src, size_t _count);

		// bytes per row of an LcdBitmap
		constexpr size_t bitmapStride(const uint32_t _width) { return (_width + 7) / 8; }

		// _count nibbles of _value, most significant first
		void pushNibbles(SysexBuffer& _dst, uint64_t _value, uint32_t _count);
		uint64_t readNibbles(const uint8_t* _src, uint32_t _count);

		// Each appends one frame with source Internal. Editors send theirs through pluginLib::Controller::sendFrontPanel
		void sendLcdText(std::vector<SMidiEvent>& _dst, uint32_t _cols, uint32_t _rows, const uint8_t* _cells);
		void sendLcdCgRam(std::vector<SMidiEvent>& _dst, const std::array<uint8_t, g_cgRamSize>& _cgRam);
		// _pixels: _height rows of bitmapStride(_width) bytes
		void sendLcdBitmap(std::vector<SMidiEvent>& _dst, uint32_t _width, uint32_t _height, const uint8_t* _pixels);
		void sendLedMask(std::vector<SMidiEvent>& _dst, const std::vector<bool>& _leds);
		void sendButton(std::vector<SMidiEvent>& _dst, uint32_t _index, bool _pressed);
		void sendEncoder(std::vector<SMidiEvent>& _dst, uint32_t _index, int32_t _delta);
		void sendParam(std::vector<SMidiEvent>& _dst, uint8_t _page, uint8_t _index, int32_t _value);
		void sendRequestState(std::vector<SMidiEvent>& _dst, uint8_t _mask = 0);
	}

	// Decodes front panel frames into events. LcdText, LcdCgRam, LcdBitmap and LedMask are retained, a listener added later gets
	// the current state at once. A device owns one and calls receive() before anything else sees the sysex, the editor
	// side owns one in pluginLib::Controller
	class FrontPanelTransport
	{
	public:
		baseLib::Event<uint32_t, uint32_t, std::vector<uint8_t>> evLcdText;	// cols, rows, cells
		baseLib::Event<std::array<uint8_t, fp::g_cgRamSize>> evLcdCgRam;
		baseLib::Event<uint32_t, uint32_t, std::vector<uint8_t>> evLcdBitmap;	// width, height, pixels as in the frame
		baseLib::Event<std::vector<bool>> evLedMask;
		baseLib::Event<uint32_t, bool> evButton;							// index, pressed
		baseLib::Event<uint32_t, int32_t> evEncoder;						// index, delta
		baseLib::Event<uint8_t, uint8_t, int32_t> evParam;					// page, index, value
		baseLib::Event<uint8_t> evRequestState;								// mask
		baseLib::Event<uint8_t, SysexBuffer> evProduct;						// type, the whole frame

		// True for every front panel frame, understood or not (unknown type, bad length): none may reach a firmware
		bool receive(const SysexBuffer& _sysex);
	};
}
