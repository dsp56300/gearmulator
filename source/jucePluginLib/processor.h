#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "bypassBuffer.h"
#include "controller.h"
#include "midiports.h"

#include "synthLib/plugin.h"

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace synthLib
{
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
			const bool isSynth;
			const bool wantsMidiInput;
			const bool producesMidiOut;
			const bool isMidiEffect;
			const std::string lv2Uri;
			BinaryDataRef binaryData;
		};

		Processor(const BusesProperties& _busesProperties, Properties _properties);
		~Processor() override;

		void addMidiEvent(const synthLib::SMidiEvent& ev);

		void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message);

	    Controller& getController();
		bool isPluginValid() { return getPlugin().isValid(); }

		synthLib::Plugin& getPlugin();

		virtual synthLib::Device* createDevice() = 0;

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

		bool setPreferredDeviceSamplerate(float _samplerate);
		float getPreferredDeviceSamplerate() const;
		std::vector<float> getDeviceSupportedSamplerates() const;
		std::vector<float> getDevicePreferredSamplerates() const;

		float getHostSamplerate() const { return m_hostSamplerate; }

		const Properties& getProperties() const { return m_properties; }

		virtual void processBpm(float _bpm) {}

		bool rebootDevice();

		auto& getMidiPorts() { return m_midiPorts; }

		std::optional<std::pair<const char*, uint32_t>> findResource(const std::string& _filename) const;

		std::string getPublicRomFolder() const;

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

	protected:
		synthLib::DeviceError m_deviceError = synthLib::DeviceError::None;
		std::unique_ptr<synthLib::Device> m_device;
		std::unique_ptr<synthLib::Plugin> m_plugin;
		std::vector<synthLib::SMidiEvent> m_midiOut;

	private:
		const Properties m_properties;
		float m_outputGain = 1.0f;
		float m_inputGain = 1.0f;
		uint32_t m_dspClockPercent = 100;
		float m_preferredDeviceSamplerate = 0.0f;
		float m_hostSamplerate = 0.0f;
		MidiPorts m_midiPorts;
		BypassBuffer m_bypassBuffer;
	};
}
