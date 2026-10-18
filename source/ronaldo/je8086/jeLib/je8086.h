#pragma once

#include <array>
#include <memory>

#include "cpu/h8300h/machine.hpp"

#include "je8086devices.h"
#include "je8080devices.h"
#include "jeLcd.h"
#include "synthLib/frontPanelSysex.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiControllerThinner.h"
#include "synthLib/midiRateLimiter.h"

namespace jeLib
{
	class Je8086
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>; // left, right
		using SampleBuffer = std::vector<SampleFrame>;

		// H8/3002 at 16 MHz; the DSPs step at 33.8688 MHz (1323/625 of it).
		static constexpr uint64_t McuClock = 16'000'000;
		static constexpr uint32_t RamSize = 0x40000;

		// _ramData is the battery-backed SRAM image, RamSize bytes; see createFactoryRam()
		Je8086(const std::vector<uint8_t>& _romData, const std::vector<uint8_t>& _ramData);

		// Boots with blank SRAM and runs the front-panel factory reset, returns the resulting SRAM image
		static std::vector<uint8_t> createFactoryRam(const std::vector<uint8_t>& _romData);

		void addMidiEvent(const synthLib::SMidiEvent& _event);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);

		// Steps the machine as needed, one step can produce more than one frame
		SampleFrame renderSample();

		// The JP-8080's A/D converter, one frame per rendered sample: left INST/LOWER, right VOCAL/UPPER, 24-bit words.
		// The keyboard has no audio input.
		void setInput(const SampleFrame& _frame);

		// One sample period of the machine (the DSPs run for the same time).
		void step();

		bool isRack() const { return m_isRack; }

		void setButton(devices::SwitchType _type, bool _pressed) { m_ports.press(_type, _pressed); }
		void setButton(je8080::SwitchType _type, bool _pressed) { m_ports.press(_type, _pressed); }

		// ESP engine policy for every DSP on the board (tools and tests).
		void setEspEngine(esp::Engine _engine);

		template<typename F> void forEachEsp(F&& _func) const
		{
			if (m_rackAsics)
				m_rackAsics->forEach(_func);
			else
				m_keyboardAsics->forEach(_func);
		}

		uint64_t cycles() const { return m_machine.now(); }

	private:
		void onReceiveMidiByte(uint8_t _byte);
		void onReceiveSample(int32_t _left, int32_t _right);
		// to the input the event arrives at on the hardware, see m_remoteKeyboardRateLimiter
		void writeMidiIn(const synthLib::SMidiEvent& _ev);
		// the visible 2 x 20 LCD cells and the CGRAM to the editor
		void sendLcdText();
		void sendLcdCgRam();
		void runDsps();
		std::vector<uint8_t> runFactoryReset();

		const bool m_isRack;

		h8300h::Machine m_machine;
		std::unique_ptr<devices::KeyboardAsics> m_keyboardAsics;
		std::unique_ptr<devices::RackAsics> m_rackAsics;
		Lcd m_lcd;
		uint64_t m_lcdGeneration = 0;	// of the LCD content last sent to the UI
		devices::Port m_ports;
		devices::Faders m_faders;
		devices::KeyScanner m_keyScanner;
		devices::CatchAllDevice m_catchAll;
		devices::LineMux m_panelLine;
		devices::LineMux m_rackLcdLine;
		devices::LineMux m_rackStatusLine;
		std::unique_ptr<devices::DintStatus<devices::RackAsics>> m_rackDintStatus;
		uint64_t m_stepRemainder = 0;

		synthLib::MidiBufferParser m_midiOutParser;
		std::vector<synthLib::SMidiEvent> m_midiInEvents;
		SampleBuffer m_sampleBuffer;
		size_t m_sampleReadPos = 0;

		// A step runs the DSPs up to a few frames ahead of renderSample(), the converter frames wait here for them
		static constexpr uint32_t InputDelay = 4;
		std::array<SampleFrame, 64> m_input{};
		uint32_t m_inputRead = 0;
		uint32_t m_inputWrite = InputDelay;
		synthLib::MidiRateLimiter m_midiInRateLimiter;

		// The JP-8080's second MIDI input, REMOTE KBD IN, on SCI1. Only it plays the arpeggiator and RPS, and it handles
		// what it receives like the front panel. The plugin has one input, so the firmware's Remote KBD CH picks what
		// goes here, as the JP-8000's Remote Ctrl CH does on its single input
		synthLib::MidiRateLimiter m_remoteKeyboardRateLimiter;

		// A continuous controller reaches the wire at most this often per channel and
		// controller: 250 times a second, what a hardware controller sends. A host sends
		// them as densely as it likes, and the limiter above loses nothing, so a dense
		// stream simply queues - at 31.25 kbaud a CC costs ~1 ms, and a stream beyond the
		// wire rate delays every note played meanwhile without bound (BUG-10307). The
		// divisor is the rate onReceiveSample runs at, the same one the limiter is given.
		static constexpr uint32_t ControllerIntervalSamples = 88200 / 250;

		// Thins dense controller streams before they reach the rate limiter. Fed in
		// step(), flushed and clocked in onReceiveSample(). Notes and everything else
		// pass untouched, and a held value goes out before the next message on its channel.
		synthLib::MidiControllerThinner m_controllerThinner{ControllerIntervalSamples,
			[this](const synthLib::SMidiEvent& _ev)
		{
			writeMidiIn(_ev);
		}};
		uint32_t m_controllerThinnerClock = 0;

		std::vector<synthLib::SMidiEvent> m_midiOutEvents;
		synthLib::FrontPanelTransport m_frontPanel;
	};
}
