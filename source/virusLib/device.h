#pragma once

#include "dspSingle.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/device.h"

#include "midiOutParser.h"
#include "romfile.h"
#include "microcontroller.h"

namespace virusLib
{
	class Device final : public synthLib::Device
	{
	public:
		Device(const ROMFile& _rom);
		~Device() override;

		float getSamplerate() const override;
		bool isValid() const override;

		void process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut) override;

		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;

		uint32_t getInternalLatencyMidiToOutput() const override;
		uint32_t getInternalLatencyInputToOutput() const override;

		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;

		static void createDspInstances(DspSingle*& _dspA, DspSingle*& _dspB, const ROMFile& _rom);
		static std::thread bootDSP(DspSingle& _dsp, const ROMFile& _rom);

	private:
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		void onAudioWritten();
		static void configureDSP(DspSingle& _dsp, const ROMFile& _rom);

		const ROMFile& m_rom;

		std::unique_ptr<DspSingle> m_dsp;
		DspSingle* m_dsp2 = nullptr;
		std::unique_ptr<Microcontroller> m_mc;

		MidiOutParser m_midiOutParser;

		uint32_t m_numSamplesWritten = 0;
		uint32_t m_numSamplesProcessed = 0;
	};
}
