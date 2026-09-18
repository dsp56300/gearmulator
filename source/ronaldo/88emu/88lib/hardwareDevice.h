#pragma once

#include "88lib/analog/analogOutput.h"
#include "88lib/deviceModel.h"

#include "synthLib/device.h"
#include "synthLib/midiBufferParser.h"
#include "synthLib/midiRateLimiter.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace emu88Lib
{
	class Sc88;
	class Sc88Pro;
	class Sc8850;
	class Sc8820;
	class Cm32p;
	class Cm32l;
	class Cm64;
	class Sc55Board;

	// Offline work the device does at construction, before anyone can hear or see it.
	struct BootOptions
	{
		// Drive the firmware's own factory initialization on the boards that have one; they then
		// power-cycle and boot from it.
		bool factoryReset = true;
		// Then keep the board running for another 10 s, so it starts past its power-on intro.
		bool fastBoot = false;
		// Held switches at power-on; bypass automatic initialization and intro skipping.
		uint32_t initialPanelButtons = 0;
	};

	// Runs a board and exposes audio, MIDI and front-panel snapshots.
	class HardwareDevice final : public synthLib::Device
	{
	public:
		struct DisplaySnapshot
		{
			enum class Type : uint8_t { None, Character, Graphic };

			// One display panel. A Character screen hands over the controller's memory for the
			// SC-88 panel renderer; a Graphic one is already a dot grid, width x height, row major.
			struct Screen
			{
				Type type = Type::None;
				std::array<uint8_t, 80> ddRam{};
				std::array<uint8_t, 64> cgRam{};
				std::vector<uint8_t> mono;
				uint16_t width = 0;
				uint16_t height = 0;
				bool displayOn = false;
			};

			// Only a board with two panels fills the second - see deviceHasSecondLcd(), which is
			// the CM-64 and its two service displays.
			std::array<Screen, 2> screens;
			uint16_t leds = 0;
			uint64_t revision = 0;
		};

		// _pcmCard is a raw card image for a board with a PCM card slot (the CM-32P, and the
		// CM-64's PCM half); empty leaves the slot empty.
		explicit HardwareDevice(const synthLib::DeviceCreateParams& _params, const BootOptions& _boot = {},
		                        const std::vector<uint8_t>& _pcmCard = {});
		// Whether the CM-32P can read _image as a PCM card.
		static bool isPcmCardImage(const std::vector<uint8_t>& _image);
		~HardwareDevice() override;

		float getSamplerate() const override;
		void getSupportedSamplerates(std::vector<float>& _dst) const override;
		bool setSamplerate(float _samplerate) override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override;
		DeviceModel model() const { return m_model; }
		void setPanelButtons(uint32_t _buttons);
		void turnPanelEncoder(int32_t _detents);
		DisplaySnapshot displaySnapshot() const;

		// Selects the circuit after the DAC. A model with the current oversampling takes over at
		// once; one that changes it waits for the engine to set the matching rate through
		// setSamplerate(). Must not run concurrently with process().
		void setAnalogOutputMode(AnalogOutputMode _mode);
		AnalogModel analogModel() const { return m_analogOutput.model(); }

	protected:
		void onTransportDiscontinuity(const synthLib::SMidiEvent& _event) override { m_midiIn.push_back(_event); }
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs,
		                  size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _event,
		              std::vector<synthLib::SMidiEvent>&) override;

	private:
		enum class PanelCommandType : uint8_t { Buttons, Encoder };
		struct PanelCommand
		{
			PanelCommandType type = PanelCommandType::Buttons;
			int32_t value = 0;
		};

		float dacSamplerate() const;
		void activateAnalogModel(AnalogModel _model);
		// Renders one DAC frame into m_heldFrame, and into m_heldFrameB where the board has a
		// second path.
		void renderBoardFrame();
		void writeOutputSample(const synthLib::TAudioOutputs& _outputs, size_t _index);
		void sendMidiToBoard(const synthLib::SMidiEvent& _event);
		void readMidiOutFromBoard(std::vector<synthLib::SMidiEvent>& _midiOut);
		void collectPanelCommands();
		void applyDuePanelCommand();
		void publishDisplaySnapshot();
		void handleTransportDiscontinuity(uint32_t generation);
		void silenceActiveChannels();
		void trackMidiActivity(const synthLib::SMidiEvent& event);

		DeviceModel m_model = DeviceModel::Sc88Pro;
		std::unique_ptr<Sc88> m_sc88;
		std::unique_ptr<Sc88Pro> m_sc88Pro;
		std::unique_ptr<Sc8850> m_sc8850;
		std::unique_ptr<Sc8820> m_sc8820;
		std::unique_ptr<Cm32p> m_cm32p;
		std::unique_ptr<Cm32l> m_cm32l;
		std::unique_ptr<Cm64> m_cm64;
		std::unique_ptr<Sc55Board> m_sc55;
		std::unique_ptr<synthLib::MidiRateLimiter> m_sc55MidiIn;
		std::array<synthLib::MidiBufferParser, 2> m_sc88ProMidiOut{
			synthLib::MidiBufferParser{synthLib::MidiEventSource::Device},
			synthLib::MidiBufferParser{synthLib::MidiEventSource::Device}};
		std::array<size_t, 2> m_sc88ProMidiOutOffsets{};
		synthLib::MidiBufferParser m_sc55MidiOut{synthLib::MidiEventSource::Device};
		std::vector<uint8_t> m_sc55MidiOutBytes;
		std::vector<synthLib::SMidiEvent> m_sc88MidiOut;
		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		mutable std::mutex m_panelMutex;
		std::deque<PanelCommand> m_pendingPanelCommands;
		std::deque<PanelCommand> m_panelCommands;
		uint64_t m_nextPanelCommandSample = 0;
		uint64_t m_renderedSamples = 0;
		uint32_t m_transportGeneration = 0;
		uint64_t m_activeChannels = 0;

		mutable std::mutex m_displayMutex;
		DisplaySnapshot m_display;

		AnalogOutput m_analogOutput;
		// The board's own output amplifiers, applied whatever the analog setting is: switching
		// the circuit emulation off should change the tone, not the volume.
		BoardOutputGain m_boardGain;
		AnalogModel m_selectedAnalogModel = AnalogModel::None;
		uint8_t m_dacBits = synthLib::DacInterfaceBits;
		// Output samples already produced from the held DAC frame. A board that is two boards
		// summed keeps the second half in m_heldFrameB, so each can be filtered on its own way
		// to the mixer; everything else leaves it zero and never looks at it.
		uint32_t m_holdPhase = 0;
		std::pair<int32_t, int32_t> m_heldFrame{};
		std::pair<int32_t, int32_t> m_heldFrameB{};
	};
}
