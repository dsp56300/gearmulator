#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audioLevels.h"
#include "bypassBuffer.h"
#include "controller.h"
#include "hostAudioWorkgroup.h"
#include "midiLearnTranslator.h"
#include "midiports.h"
#include "programChangeRouter.h"
#include "midiNotifier.h"
#include "skinVariables.h"

#include "baseLib/event.h"

#include "bridgeLib/types.h"

#include "synthLib/midiRoutingMatrix.h"
#include "synthLib/plugin.h"

namespace bridgeClient
{
	class RemoteDevice;
}

namespace bridgeLib
{
	struct PluginDesc;
}

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace synthLib
{
	struct DeviceCreateParams;
	class Plugin;
	struct SMidiEvent;
}

namespace pluginLib
{
	class Processor : public juce::AudioProcessor, juce::AsyncUpdater
	{
	public:
		struct BinaryDataRef
		{
			uint32_t listSize = 0;
			const char** originalFileNames = nullptr;
			const char** namedResourceList = nullptr;
			const char* (*getNamedResourceFunc)(const char*, int&) = nullptr;
		};

		struct Properties
		{
			const std::string name;
			const std::string vendor;
			const bool isSynth;
			const bool wantsMidiInput;
			const bool producesMidiOut;
			const bool isMidiEffect;
			const std::string plugin4CC;
			const std::string lv2Uri;
			BinaryDataRef binaryData;
		};

		Processor(const BusesProperties& _busesProperties, Properties _properties);
		~Processor() override;

		void addMidiEvent(const synthLib::SMidiEvent& _ev);

		void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message);

	    Controller& getController();
		bool isPluginValid() { return getPlugin().isValid(); }

		synthLib::Plugin& getPlugin();

		ProgramChangeRouter& getProgramChangeRouter() { return m_programChangeRouter; }

		// creates the local device from getRemoteDeviceParams(), so that a local and a remote device boot the same ROM
		virtual synthLib::Device* createDevice();
		// the product's device class, a distinct name: a third createDevice overload would hide the other two
		virtual synthLib::Device* newDevice(const synthLib::DeviceCreateParams& _params) = 0;
		virtual bridgeClient::RemoteDevice* createRemoteDevice(const synthLib::DeviceCreateParams& _params);
		virtual void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const;
		virtual bridgeClient::RemoteDevice* createRemoteDevice();
		synthLib::Device* createDevice(DeviceType _type);

		bool hasController() const
		{
			return m_controller.get();
		}

		virtual bool setLatencyBlocks(uint32_t _blocks);
		uint32_t getCurrentLatency();
		virtual void updateLatencySamples();

		// Telling the host about a latency change means calling back into it, which must not
		// happen from the audio thread - a VST3 host answers restartComponent() by suspending
		// and re-preparing the plugin, re-entering us from inside process(). The audio thread
		// only compares two integers and hands the publishing to the message thread.
		void handleAsyncUpdate() override;

		virtual void saveCustomData(std::vector<uint8_t>& _targetBuffer);
		virtual void saveChunkData(baseLib::BinaryStream& s);
		virtual bool loadCustomData(const std::vector<uint8_t>& _sourceBuffer);
		virtual void loadChunkData(baseLib::ChunkReader& _cr);

		void readGain(baseLib::BinaryStream& _s);

		template<size_t N> void applyOutputGain(std::array<float*, N>& _buffers, const size_t _numSamples)
		{
			applyGain(_buffers, _numSamples, getOutputGain());
		}

		template<size_t N> static void applyGain(std::array<float*, N>& _buffers, const size_t _numSamples, const float _gain)
		{
			if(_gain == 1.0f)
				return;

			if(!_numSamples)
				return;

			for (float* buf : _buffers)
			{
				if (buf)
				{
					for (size_t i = 0; i < _numSamples; ++i)
						buf[i] *= _gain;
				}
			}
		}
		
		float getOutputGain() const { return m_outputGain; }
		void setOutputGain(const float _gain) { m_outputGain = _gain; }

		// Teardown starts here: no MIDI reaches the translator, the controller or the device afterwards, and calls
		// that are in progress have returned. Idempotent; destroyEditorState() and the destructor call it.
		void shutdown();
		
		bool setDspClockPercent(uint32_t _percent = 100);
		uint32_t getDspClockPercent() const;
		uint64_t getDspClockHz() const;
		bool canModifyDspClock() const;

		bool setPreferredDeviceSamplerate(float _samplerate);
		float getPreferredDeviceSamplerate() const;
		std::vector<float> getDeviceSupportedSamplerates() const;
		std::vector<float> getDevicePreferredSamplerates() const;

		void setResamplerMode(synthLib::Resampler::Mode _mode);
		synthLib::Resampler::Mode getResamplerMode() const { return m_resamplerMode; }

		float getHostSamplerate() const { return m_hostSamplerate; }

		const Properties& getProperties() const { return m_properties; }

		virtual void processBpm(float _bpm) {}

		bool rebootDevice();
		// The reboot the user asks for: the product first looks for firmware images it has not listed yet, a ROM copied
		// into the ROM folder after the plugin was loaded for example
		bool rescanFirmwareAndReboot();

		// An optional voice expansion board of the hardware. It is saved with the plugin state, the device gets it as bit 0
		// of customData and is created again when it changes
		bool isVoiceExpansionEnabled() const { return m_voiceExpansion; }
		void setVoiceExpansion(bool _enabled);

		// A product that runs one of several firmware images (ROMs, applications) lists them with setFirmwareImages, appends
		// the ones it finds later in findNewFirmwareImages() and reads getFirmwareIndex() when it creates its device. The
		// choice is saved in a chunk with the product's own id, version and payload, before REMO and the device state, so
		// that a state switches the firmware first. Selecting another image creates the device again; if that fails, the
		// running one stays selected
		struct FirmwareChunk
		{
			char id[5];		// four characters, "ROM "
			uint32_t version = 1;
			std::function<void(baseLib::BinaryStream&, uint32_t _index)> write;
			// the index of the image the payload names, none if there is no such image
			std::function<std::optional<uint32_t>(baseLib::BinaryStream&, uint32_t _version)> find;
		};

		void setFirmwareImages(std::vector<std::string> _names, FirmwareChunk _chunk);
		const std::vector<std::string>& getFirmwareNames() const { return m_firmwareNames; }
		uint32_t getFirmwareIndex() const { return m_firmwareIndex; }
		bool selectFirmware(uint32_t _index);

		// retained, a listener gets the current index when it is added. A state loaded by the host invokes it on the
		// host's thread. Also invoked when rescanFirmwareAndReboot() found new images: without one before, the selected
		// image is a new one. For a product without a list of images, invoked after each rescanFirmwareAndReboot()
		baseLib::Event<uint32_t> evFirmwareChanged;

		auto& getMidiPorts() { return m_midiPorts; }
		auto& getSkinVariables() { return m_skinVariables; }
		auto& getMidiNotifier() { return m_midiNotifier; }

		static std::optional<std::pair<const char*, uint32_t>> findResource(const BinaryDataRef& _binaryData, const std::string& _filename);
		std::optional<std::pair<const char*, uint32_t>> findResource(const std::string& _filename) const;

		std::string getDataFolder(bool _useFxFolder = false) const;
		std::string getPublicRomFolder() const;
		std::string getConfigFolder(bool _useFxFolder = false) const;
		std::string getPatchManagerDataFolder(bool _useFxFolder = false) const;
		std::string getConfigFile(bool _useFxFolder = false) const;
		std::string getProductName(bool _useFxName = false) const;

		void getPluginDesc(bridgeLib::PluginDesc& _desc) const;

		void setDeviceType(DeviceType _type, bool _forceChange = false);
		void setRemoteDevice(const std::string& _host, uint32_t _port);
		const auto& getRemoteDeviceHost() const { return m_remoteHost; }
		const auto& getRemoteDevicePort() const { return m_remotePort; }

		auto getDeviceType() const { return m_deviceType; }

		const synthLib::MidiRoutingMatrix& getMidiRoutingMatrix() const { return m_midiRoutingMatrix; }
		synthLib::MidiRoutingMatrix& getMidiRoutingMatrix() { return m_midiRoutingMatrix; }

		MidiLearnTranslator* getMidiLearnTranslator() { return m_midiLearnTranslator.get(); }

		std::string getMidiLearnFolder() const;
		void saveDefaultMidiLearnPreset();
		void loadDefaultMidiLearnPreset();

	protected:
		void destroyController();

		// Looks for the firmware images again, appends the ones not listed yet and returns their names. A listed image
		// keeps its index, so the selected one stays selected. Called on the message thread
		virtual std::vector<std::string> findNewFirmwareImages() { return {}; }

	private:
		void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
		void releaseResources() override;
		void processorLayoutsChanged() override;

		// The device output of each output channel. The output buses carry the outputs in the order they are declared,
		// whether the host turned some off or not. A bus declared mono carries one output alone, the n-th of them the
		// n-th output: a product offers its outputs singly that way, beside the stereo pairs, off by default. Empty while
		// each channel carries its own, in order
		void updateOutputMap();

		//==============================================================================
		bool isBusesLayoutSupported(const BusesLayout&) const override;
	    void getStateInformation (juce::MemoryBlock& destData) override;
	    void setStateInformation (const void* _data, int _sizeInBytes) override;
	    void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	    void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;
		const juce::String getName() const override;
		bool acceptsMidi() const override;
		bool producesMidi() const override;
		bool isMidiEffect() const override;
		void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
		void processBlockBypassed(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midiMessages) override;

	public:
		// ---- Audio capture (for automated audio verification, driven by the MCP server) ----
		struct AudioCaptureResult
		{
			bool valid = false;			// a capture buffer existed
			bool started = false;		// recording actually began (a note arrived, if armed on note)
			uint32_t frames = 0;		// number of captured frames
			uint32_t channels = 0;
			double sampleRate = 0.0;
			float peak = 0.0f;			// absolute peak across the capture (≈0 means the device was silent)
			float rms = 0.0f;
		};

		// Record the main stereo output into a pre-allocated, hard-capped buffer. _maxFrames is clamped to the
		// capacity (~30s, allocated in prepareToPlay) so a capture that is never stopped cannot run unbounded.
		// If _armOnNote, recording begins on the next played note-on instead of immediately.
		void startAudioCapture(uint32_t _maxFrames, bool _armOnNote);
		// Stop capture, write the captured audio to a 16-bit WAV at _wavPath, and return level stats.
		AudioCaptureResult stopAudioCapture(const std::string& _wavPath);
		bool isAudioCaptureActive() const;
		uint32_t getAudioCaptureCapacityFrames() const { return static_cast<uint32_t>(m_captureBuffer.getNumSamples()); }

		// levels of the output channels, for meters in skins
		AudioLevels& getOutputLevels() { return m_outputLevels; }

		// ---- Test input (for automated verification of effects and audio inputs, driven by the MCP server) ----
		struct TestInputResult
		{
			bool valid = false;				// the file was read and plays now
			std::string error;				// why not
			uint32_t frames = 0;			// length of the file, at its own rate
			uint32_t channels = 0;			// of the file
			double fileSampleRate = 0.0;
		};

		// Feed a WAV or AIFF file into the audio inputs instead of what the host sends, converted to the host sample
		// rate while it plays, so that a host that tells its rate only later or changes it gets it right too. A mono
		// file feeds every input. Once a file without _loop has ended the inputs stay silent until stopTestInput
		// gives them back to the host.
		TestInputResult startTestInput(const std::string& _path, bool _loop);
		// Returns false if there was no test input
		bool stopTestInput();

	private:
		void setState(const void *_data, size_t _sizeInBytes);

	    //==============================================================================
		int getNumPrograms() override;
		int getCurrentProgram() override;
		void setCurrentProgram(int _index) override;
		const juce::String getProgramName(int _index) override;
		void changeProgramName(int _index, const juce::String &_newName) override;

	    //==============================================================================
		double getTailLengthSeconds() const override;
		//==============================================================================
		virtual Controller *createController() = 0;

	    std::unique_ptr<Controller> m_controller{};

		synthLib::DeviceError getDeviceError() const { return m_deviceError; }

		// audio thread, see synthLib::Plugin::CallbackDeviceInvalid
		void onDeviceInvalid(synthLib::Device* _device);

	protected:
		synthLib::DeviceError m_deviceError = synthLib::DeviceError::None;
		// Guards the lazy creation of m_device/m_plugin in getPlugin(). Without it, two concurrent
		// first-callers (e.g. the audio thread and an MCP request thread) both see m_plugin == null during
		// the seconds-long device boot and both run m_device.reset(createDevice()), so the second reset
		// destroys the device the first is still booting - a use-after-free that corrupts the heap.
		std::mutex m_deviceCreateMutex;
		std::unique_ptr<synthLib::Device> m_device;
		std::unique_ptr<synthLib::Plugin> m_plugin;
		std::vector<synthLib::SMidiEvent> m_midiOut;

	private:
		void addHostMidiFeedback(const synthLib::SMidiEvent& _event);
		void replaceInvalidDevice(const synthLib::Device* _device);

		const Properties m_properties;
		std::atomic<float> m_outputGain{1.0f};
		std::atomic<float> m_inputGain{1.0f};
		uint32_t m_dspClockPercent = 100;
		bool m_voiceExpansion = false;
		bool m_deviceVoiceExpansion = false;	// the voice expansion the current device was created with
		std::vector<std::string> m_firmwareNames;
		FirmwareChunk m_firmwareChunk{};
		uint32_t m_firmwareIndex = 0;
		float m_preferredDeviceSamplerate = 0.0f;
		synthLib::Resampler::Mode m_resamplerMode = synthLib::Resampler::Mode::MameHq;
		float m_hostSamplerate = 0.0f;
		MidiPorts m_midiPorts;
		SkinVariables m_skinVariables;
		MidiNotifier m_midiNotifier;
		BypassBuffer m_bypassBuffer;
		std::vector<uint32_t> m_outputMap;	// see updateOutputMap
		juce::AudioBuffer<float> m_outputScratch;	// what the device renders while there is an output map
		DeviceType m_deviceType = DeviceType::Local;
		std::string m_remoteHost;
		uint32_t m_remotePort = 0;
		bridgeLib::SessionId m_remoteSessionId;
		std::atomic<uint32_t> m_reportedLatency{0};
		synthLib::MidiRoutingMatrix m_midiRoutingMatrix;
		std::string m_programName;
		std::unique_ptr<MidiLearnTranslator> m_midiLearnTranslator;
		ProgramChangeRouter m_programChangeRouter;
		HostAudioWorkgroup m_hostAudioWorkgroup;

		// addMidiEvent() runs on the audio thread, MIDI port threads and the MCP server, and it calls itself through
		// MIDI learn. shutdown() raises the flag and waits for the count to drop to zero
		std::atomic<bool> m_shutdown{false};
		std::atomic<uint32_t> m_midiInputCalls{0};

		// lambdas posted to the message thread hold a weak reference to it, it goes away in shutdown()
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

		// Host MIDI feedback queue (filled from parameter listeners, drained in processBlock)
		std::mutex m_hostFeedbackMutex;
		std::vector<synthLib::SMidiEvent> m_hostFeedbackQueue;

		// ---- Audio capture state (see startAudioCapture). The buffer is allocated once per sample rate in
		// prepareToPlay and only written by the audio thread while Recording, so start/stop are lock-free. ----
		enum class CaptureState { Idle, Armed, Recording, Done };
		void captureAudioBlock(const juce::AudioBuffer<float>& _buffer, int _numSamples);
		void audioCaptureCheckArm(const synthLib::SMidiEvent& _ev);
		static constexpr int g_captureChannels = 2;
		juce::AudioBuffer<float> m_captureBuffer;
		std::atomic<CaptureState> m_captureState{CaptureState::Idle};
		std::atomic<int> m_capturePos{0};
		std::atomic<int> m_captureMaxFrames{0};
		std::atomic<bool> m_captureStarted{false};

		AudioLevels m_outputLevels;

		// ---- Test input state (see startTestInput). The audio thread takes the current one with std::atomic_load
		// and never blocks. A replaced one waits in m_testInputPrev, so that the audio thread does not free it ----
		struct TestInput
		{
			juce::AudioBuffer<float> audio;		// the file as it is
			double sampleRate = 0.0;			// of the file
			bool loop = false;
			std::vector<juce::LagrangeInterpolator> resamplers;	// one per file channel, for a host at another rate
			std::atomic<int> pos{0};			// in the file
		};
		void injectTestInput(juce::AudioBuffer<float>& _buffer, int _numChannels, int _numSamples) const;
		std::shared_ptr<TestInput> m_testInput;
		std::shared_ptr<TestInput> m_testInputPrev;
	};
}
