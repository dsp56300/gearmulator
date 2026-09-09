#pragma once

#include "deviceModel.h"

#include "synthLib/device.h"
#include "synthLib/midiBufferParser.h"

#include <array>
#include <atomic>
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
	class Sc55Mk2;
	class Sc88Thread;

	// Runs a board and exposes audio, MIDI and front-panel snapshots.
	class HardwareDevice final : public synthLib::Device
	{
	public:
		struct DisplaySnapshot
		{
			enum class Type : uint8_t { None, Character, Graphic };

			Type type = Type::None;
			std::array<uint8_t, 80> ddRam{};
			std::array<uint8_t, 64> cgRam{};
			std::vector<uint8_t> mono;
			uint16_t width = 0;
			uint16_t height = 0;
			uint8_t leds = 0;
			bool displayOn = false;
			uint64_t revision = 0;
		};

		explicit HardwareDevice(const synthLib::DeviceCreateParams& _params);
		~HardwareDevice() override;

		float getSamplerate() const override;
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

		std::pair<int32_t, int32_t> renderBoardSample();
		void sendMidiToBoard(const synthLib::SMidiEvent& _event);
		void readMidiOutFromBoard(std::vector<synthLib::SMidiEvent>& _midiOut);
		void beforeWorkerJob();
		void applyDuePanelCommand();
		void publishDisplaySnapshot();

		DeviceModel m_model = DeviceModel::Sc88Pro;
		std::unique_ptr<Sc88> m_sc88;
		std::unique_ptr<Sc88Pro> m_sc88Pro;
		std::unique_ptr<Sc8850> m_sc8850;
		std::unique_ptr<Sc55Mk2> m_sc55;
		std::unique_ptr<Sc88Thread> m_thread;
		std::array<synthLib::MidiBufferParser, 2> m_sc88ProMidiOut{
			synthLib::MidiBufferParser{synthLib::MidiEventSource::Device},
			synthLib::MidiBufferParser{synthLib::MidiEventSource::Device}};
		std::array<size_t, 2> m_sc88ProMidiOutOffsets{};
		synthLib::MidiBufferParser m_sc8850MidiOut{synthLib::MidiEventSource::Device};
		std::vector<uint8_t> m_sc8850MidiOutBytes;
		synthLib::MidiBufferParser m_sc55MidiOut{synthLib::MidiEventSource::Device};
		std::vector<uint8_t> m_sc55MidiOutBytes;
		std::vector<synthLib::SMidiEvent> m_sc88MidiOut;
		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		mutable std::mutex m_panelMutex;
		std::deque<PanelCommand> m_pendingPanelCommands;
		std::deque<PanelCommand> m_workerPanelCommands;
		uint64_t m_nextPanelCommandSample = 0;
		uint64_t m_renderedSamples = 0;

		mutable std::mutex m_displayMutex;
		DisplaySnapshot m_display;
	};
}
