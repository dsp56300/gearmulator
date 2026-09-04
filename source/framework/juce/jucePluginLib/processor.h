#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>

#include "bypassBuffer.h"
#include "controller.h"
#include "midiLearnTranslator.h"
#include "midiports.h"
#include "programChangeRouter.h"

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
	class Processor : public juce::AudioProcessor
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

		virtual synthLib::Device* createDevice() = 0;
		virtual bridgeClient::RemoteDevice* createRemoteDevice(const synthLib::DeviceCreateParams& _params);
		virtual void getRemoteDeviceParams(synthLib::DeviceCreateParams& _params) const;
		virtual bridgeClient::RemoteDevice* createRemoteDevice();
		synthLib::Device* createDevice(DeviceType _type);

		bool hasController() const
		{
			return m_controller.get();
		}

		virtual bool setLatencyBlocks(uint32_t _blocks);
		virtual void updateLatencySamples();

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
		
		bool setDspClockPercent(uint32_t _percent = 100);
		uint32_t getDspClockPercent() const;
		uint64_t getDspClockHz() const;
		bool canModifyDspClock() const;

		/* How many threads the device may spread its DSP over. 1 (the default)
		 * means it cannot, and the setting is hidden. Applied when the device is
		 * created, because it fixes the reported latency. */
		uint32_t getMaxDspThreads() const;
		uint32_t getDspThreads() const { return m_dspThreads; }
		virtual bool setDspThreads(uint32_t _threads);

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

		auto& getMidiPorts() { return m_midiPorts; }

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

	private:
		void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
		void releaseResources() override;

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

	private:
#if !SYNTHLIB_DEMO_MODE
		void setState(const void *_data, size_t _sizeInBytes);
#endif

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

		synthLib::Device* onDeviceInvalid(synthLib::Device* _device);

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

		const Properties m_properties;
		float m_outputGain = 1.0f;
		float m_inputGain = 1.0f;
		uint32_t m_dspClockPercent = 100;
		uint32_t m_dspThreads = 0;
		float m_preferredDeviceSamplerate = 0.0f;
		synthLib::Resampler::Mode m_resamplerMode = synthLib::Resampler::Mode::Legacy;
		float m_hostSamplerate = 0.0f;
		MidiPorts m_midiPorts;
		BypassBuffer m_bypassBuffer;
		DeviceType m_deviceType = DeviceType::Local;
		std::string m_remoteHost;
		uint32_t m_remotePort = 0;
		bridgeLib::SessionId m_remoteSessionId;
		synthLib::MidiRoutingMatrix m_midiRoutingMatrix;
		std::string m_programName;
		std::unique_ptr<MidiLearnTranslator> m_midiLearnTranslator;
		ProgramChangeRouter m_programChangeRouter;

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
	};
}
