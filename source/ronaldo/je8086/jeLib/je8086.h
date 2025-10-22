#pragma once

#include <h8s/h8sdevices.hpp>

#include "je8086devices.h"
#include "jeLcd.h"
#include "sysexRemoteControl.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

namespace jeLib
{
	class Je8086
	{
	public:
		using SampleFrame = std::pair<int32_t, int32_t>; // left, right
		using SampleBuffer = std::vector<SampleFrame>;

		Je8086(const std::vector<uint8_t>& _romData, const std::string& _ramDataFilename);
		~Je8086() = default;

		void addMidiEvent(const synthLib::SMidiEvent& _event);
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _events);

		const auto& getSampleBuffer() const { return m_sampleBuffer; }
		void clearSampleBuffer() { m_sampleBuffer.clear(); }

		void step();

		bool hasDoneFactoryReset() const { return m_factoryreset; }

		void setButton(devices::SwitchType _type, bool _pressed);

	private:
		static void onLedsChanged(devices::Port* _port);
		void onReceiveMidiByte(uint8_t _byte);
		void onReceiveSample(int32_t _left, int32_t _right);
		void onLcdDdRamChanged();
		void onLcdCgRamChanged();

		void runfactoryreset(const std::string& _ramDataFilename);

		H8SEmulator emu;
		devices::MultiAsic asics;
		Lcd lcd;
		devices::Port ports;
		devices::Faders faders;
		HWRegs hwregs;
		Serial midi;
		devices::KeyScanner keyscanner;
		Timers timers;
		CatchAllDevice catchall;
		int ctr {0};

		bool m_factoryreset = false;

		synthLib::MidiBufferParser m_midiOutParser;
		std::vector<synthLib::SMidiEvent> m_midiInEvents;
		SampleBuffer m_sampleBuffer;
		synthLib::MidiRateLimiter m_midiInRateLimiter;
		std::vector<synthLib::SMidiEvent> m_midiOutEvents;
	};
}
