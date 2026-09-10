#pragma once

#include "Emu88MidiPlayer.h"

#include "88lib/hardwareDevice.h"

#include "synthLib/plugin.h"
#include "synthLib/resampler.h"
#include "synthLib/wavWriter.h"

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_core/juce_core.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace emu88Player
{
	class PortMidiBridge;

	class Processor final : public juce::AudioProcessor, juce::AsyncUpdater
	{
	public:
		Processor();
		~Processor() override;

		void prepareToPlay(double _sampleRate, int _maximumBlockSize) override;
		void releaseResources() override {}
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi) override;

		// The device can change its clock at any time, but switching the resampler over builds
		// and prewarms new filters. processBlock() only notes the new rate down; this applies it.
		void handleAsyncUpdate() override;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }
		const juce::String getName() const override { return "88emuPlayer"; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return true; }
		bool isMidiEffect() const override { return false; }
		double getTailLengthSeconds() const override { return 600.0; }
		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}
		void getStateInformation(juce::MemoryBlock& _destination) override { _destination.reset(); }
		void setStateInformation(const void*, int) override {}

		emu88Lib::HardwareDevice* hardware() const { return m_device.get(); }
		MidiPlayer& midiPlayer() { return m_midiPlayer; }
		const MidiPlayer& midiPlayer() const { return m_midiPlayer; }
		static constexpr float kMinimumOutputGain = 0.0f;
		static constexpr float kUnityOutputGain = 1.0f;
		static constexpr float kMaximumOutputGain = 2.0f;
		float outputGain() const { return m_outputGain.load(std::memory_order_relaxed); }
		void setOutputGain(float _gain);
		synthLib::Resampler::Mode resamplerMode() const;
		void setResamplerMode(synthLib::Resampler::Mode _mode);
		bool portMidiEnabled() const { return m_portMidiEnabled.load(std::memory_order_acquire); }
		void setPortMidiEnabled(bool _enabled);
		emu88Lib::DeviceModel deviceModel() const { return m_deviceModel; }
		bool setDeviceModel(emu88Lib::DeviceModel _model);
		bool restartDevice();
		void sendGmReset();
		void sendGsReset();
		void sendAllNotesOff();

		// Capture audio to a scratch WAV on a writer thread until the user saves it.
		bool isRecording() const { return m_recording.load(std::memory_order_relaxed); }
		bool startRecording();
		juce::File stopRecording();
		std::unique_ptr<juce::PropertiesFile> takeConfigOwnership();
		void useConfig(juce::PropertiesFile& _config);

		juce::PropertiesFile& config() const { return *m_config; }
		const std::string& dataFolder() const { return m_dataFolder; }
		const std::string& romFolder() const { return m_romFolder; }
		bool hasValidRom() const { return m_device && m_device->isValid(); }

		// A board can only be selected once every ROM the registry lists for it
		// is present; the device menu offers the rest greyed out.
		static bool isModelAvailable(emu88Lib::DeviceModel _model);
		// The board to fall back to when the configured one is not available.
		static std::optional<emu88Lib::DeviceModel> firstAvailableModel();

	private:
		synthLib::DeviceCreateParams createDeviceParams(emu88Lib::DeviceModel _model) const;
		static std::unique_ptr<juce::PropertiesFile> createConfig(const std::string& _dataFolder);
		bool replaceDevice(emu88Lib::DeviceModel _model, bool _persistModel);
		uint8_t midiPortCount() const;
		void sendSystemExclusive(std::initializer_list<uint8_t> _bytes);
		void sendMidiEvents(const std::vector<synthLib::SMidiEvent>& _events);
		void recordBlock(const juce::AudioBuffer<float>& _buffer);

		std::string m_dataFolder;
		std::string m_romFolder;
		std::unique_ptr<juce::PropertiesFile> m_ownedConfig;
		juce::PropertiesFile* m_config = nullptr;
		std::unique_ptr<emu88Lib::HardwareDevice> m_device;
		std::unique_ptr<synthLib::Plugin> m_engine;
		std::unique_ptr<PortMidiBridge> m_portMidiBridge;
		MidiPlayer m_midiPlayer;
		emu88Lib::DeviceModel m_deviceModel = emu88Lib::DeviceModel::Sc88Pro;
		std::atomic<float> m_outputGain{kUnityOutputGain};
		std::atomic<int> m_resamplerMode{static_cast<int>(synthLib::Resampler::Mode::MameHq)};
		std::atomic<bool> m_portMidiEnabled{true};

		// Touched by the audio callback, so every change is made under
		// getCallbackLock(). m_recording only mirrors m_recorder for the editor.
		std::unique_ptr<synthLib::AsyncWriter> m_recorder;
		std::vector<uint32_t> m_recordBlock;
		juce::File m_recordingFile;
		uint64_t m_recordedFrames = 0;
		std::atomic<bool> m_recording{false};

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
	};
}
