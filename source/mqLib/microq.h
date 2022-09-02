#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <thread>

#include "buttons.h"
#include "leds.h"

namespace synthLib
{
	struct SMidiEvent;
}

namespace mqLib
{
	class Hardware;

	class MicroQ
	{
	public:
		MicroQ();
		~MicroQ();

		// process a block of audio data. Be sure to pass two channels for the inputs and six channels for the outputs
		// _frames means the number of samples per channel
		void process(const float** _inputs, float** _outputs, uint32_t _frames);

		// send midi to the midi input of the device
		void sendMidi(uint8_t _byte);
		void sendMidi(uint8_t _a, uint8_t _b);
		void sendMidi(uint8_t _a, uint8_t _b, uint8_t _c);
		void sendMidi(const std::vector<uint8_t>& _buffer);
		void sendMidiEvent(const synthLib::SMidiEvent& _ev);

		// receive midi data that the device generated during the last call of process().
		// Note that any midi output data not queried between two calls of process() is lost
		void getMidiOutput(std::vector<uint8_t>& _buffer);

		// set the status of one of the front panel buttons
		void setButton(Buttons::ButtonType _button, bool _pressed);

		// rotate an encoder by a specific amount
		void rotateEncoder(Buttons::Encoders _encoder, int _amount);

		// return the state of a front panel LED. true = lit
		bool getLedState(Leds::Led _led);

		// Read the current LCD content. Returns 40 characters that represent two lines with 20 characters each
		// If the returned characters is less than 8, it is a custom character with predefined pixel data
		// You may query the pixel data via readCustomLCDCharacter below
		void readLCD(std::array<char, 40>& _data);

		// read a custom LCD character. _characterIndex needs to be < 8
		// A custom character has the dimensions 5*8 pixels
		// The _data argument receives one byte per row, with the topmost row at index 0, the bottommost row at index 7
		// Pixels per row are stored in the five LSBs of each byte, with the leftmost pixel at bit 4, the rightmost pixel at bit 0
		// A set bit indicates a set pixel
		bool readCustomLCDCharacter(std::array<uint8_t, 8>& _data, uint32_t _characterIndex);

	private:
		void processUcThread() const;

		std::unique_ptr<Hardware> m_hw;

		std::mutex m_mutex;

		std::vector<uint8_t> m_midiInBuffer;
		std::vector<uint8_t> m_midiOutBuffer;

		std::unique_ptr<std::thread> m_ucThread;
		bool m_destroy = false;
	};
}
