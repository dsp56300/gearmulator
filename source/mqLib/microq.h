#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <thread>
#include <atomic>

#include "buttons.h"
#include "leds.h"
#include "mqtypes.h"

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
		MicroQ(BootMode _bootMode = BootMode::Default);
		~MicroQ();

		// returns true if the instance is valid, false if the initialization failed
		bool isValid() const;

		// Process a block of audio data. Be sure to pass two channels for the inputs and six channels for the outputs
		// _frames means the number of samples per channel
		// _latency additional latency in samples that will be added to allow asynchronous processing
		void process(const float** _inputs, float** _outputs, uint32_t _frames, uint32_t _latency = 0);

		// Process a block of audio data. Data conversion is not performed, this allows to access raw DSP data
		// The used input and output buffers can be queried below
		void process(uint32_t _frames, uint32_t _latency = 0);

		// Retrieve the DSP audio input. Two channels, 24 bits. Only the 24 LSBs of each 32 bit word are used
		TAudioInputs& getAudioInputs();

		// Retrieve the DSP audio output. Two channels, 24 bits. Only the 24 LSBs of each 32 bit word are used
		TAudioOutputs& getAudioOutputs();

		// send midi to the midi input of the device
		void sendMidi(uint8_t _byte);
		void sendMidi(uint8_t _a, uint8_t _b);
		void sendMidi(uint8_t _a, uint8_t _b, uint8_t _c);
		void sendMidi(const std::vector<uint8_t>& _buffer);
		void sendMidiEvent(const synthLib::SMidiEvent& _ev);

		// Receive midi data that the device generated during the last call of process().
		// Note that any midi output data not queried between two calls of process() is lost
		void receiveMidi(std::vector<uint8_t>& _buffer);

		// Get the status of one of the front panel buttons
		bool getButton(Buttons::ButtonType _button);

		// Set the status of one of the front panel buttons
		void setButton(Buttons::ButtonType _button, bool _pressed);

		// retrieve the current value of a front panel encoder
		uint8_t getEncoder(Buttons::Encoders _encoder);

		// Rotate an encoder on the front panel by a specific amount
		void rotateEncoder(Buttons::Encoders _encoder, int _amount);

		// Return the state of a front panel LED. true = lit
		bool getLedState(Leds::Led _led);

		// Read the current LCD content. Returns 40 characters that represent two lines with 20 characters each
		// If the returned character is less than 8, it is a custom character with predefined pixel data
		// You may query the pixel data via readCustomLCDCharacter below
		void readLCD(std::array<char, 40>& _data);

		// Read a custom LCD character. _characterIndex needs to be < 8
		// A custom character has the dimensions 5*8 pixels
		// The _data argument receives one byte per row, with the topmost row at index 0, the bottommost row at index 7
		// Pixels per row are stored in the five LSBs of each byte, with the leftmost pixel at bit 4, the rightmost pixel at bit 0
		// A set bit indicates a set pixel.
		// As an example, the character 'P', encoded as a custom character, would look like this:
		//
		// bit       7  6  5  4  3  2  1  0
		// byte 0             *  *  *  *  -     top
		// byte 1             *  -  -  -  *
		// byte 2             *  -  -  -  *
		// byte 3             *  *  *  *  -
		// byte 4             *  -  -  -  -
		// byte 5             *  -  -  -  -
		// byte 6             *  -  -  -  -
		// byte 7             *  -  -  -  -     bottom
		//
		bool readCustomLCDCharacter(std::array<uint8_t, 8>& _data, uint32_t _characterIndex);

		// Dirty flags indicate that the front panel of the device has changed. To be retrieved via getDirtyFlags()
		enum class DirtyFlags : uint32_t
		{
			None		= 0,
			Leds		= 0x01,	// one or more LEDs changed its state
			Lcd			= 0x02,	// the LCD has been refreshed and should be repainted
			LcdCgRam	= 0x04,	// the LCD CG RAM has been modified, custom characters need to be repainted
		};

		// Retrieve dirty flags for the front panel. See enum DirtyFlags for a description
		// Dirty flags are sticky but are reset upon calling this function
		DirtyFlags getDirtyFlags();

		// Gain access to the hardware implementation, intended for advanced use. Usually not required
		Hardware* getHardware();

		// returns after the device has booted and is ready to receive midi commands
		bool isBootCompleted() const;

	private:
		void internalProcess(uint32_t _frames, uint32_t _latency);
		void onLedsChanged();
		void onLcdChanged();
		void onLcdCgRamChanged();

		void processUcThread() const;

		std::unique_ptr<Hardware> m_hw;

		std::mutex m_mutex;

		std::vector<uint8_t> m_midiInBuffer;
		std::vector<uint8_t> m_midiOutBuffer;

		std::unique_ptr<std::thread> m_ucThread;
		bool m_destroy = false;
		std::atomic<uint32_t> m_dirtyFlags = 0;
	};
}
