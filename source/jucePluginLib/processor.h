#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "controller.h"

#include "../synthLib/plugin.h"

namespace synthLib
{
	class Plugin;
	struct SMidiEvent;
}

namespace pluginLib
{
	class Processor : public juce::AudioProcessor, juce::MidiInputCallback
	{
	public:
		Processor(const BusesProperties& _busesProperties);
		~Processor() override;

		void getLastMidiOut(std::vector<synthLib::SMidiEvent>& dst);
		void addMidiEvent(const synthLib::SMidiEvent& ev);

		bool setMidiOutput(const juce::String& _out);
		juce::MidiOutput* getMidiOutput() const;
		bool setMidiInput(const juce::String& _in);
		juce::MidiInput* getMidiInput() const;

		void handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message) override;

	    Controller& getController();
		bool isPluginValid() { return getPlugin().isValid(); }

		synthLib::Plugin& getPlugin();

		virtual synthLib::Device* createDevice() = 0;

		bool hasController() const
		{
			return m_controller.get();
		}

		virtual bool setLatencyBlocks(uint32_t _blocks);
		virtual void updateLatencySamples() = 0;

		virtual void saveCustomData(std::vector<uint8_t>& _targetBuffer);
		virtual void loadCustomData(const std::vector<uint8_t>& _sourceBuffer);

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
					for (int i = 0; i < _numSamples; ++i)
						buf[i] *= _gain;
				}
			}
		}
		
		float getOutputGain() const { return m_outputGain; }
		void setOutputGain(const float _gain) { m_outputGain = _gain; }
		
		bool setDspClockPercent(int _percent = 100);
		uint32_t getDspClockPercent() const;
		uint64_t getDspClockHz() const;

	private:
		void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
		void releaseResources() override;

		//==============================================================================
	    void getStateInformation (juce::MemoryBlock& destData) override;
	    void setStateInformation (const void* _data, int _sizeInBytes) override;
	    void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	    void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

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
		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::vector<synthLib::SMidiEvent> m_midiOut{};

	private:
		float m_outputGain = 1.0f;
		float m_inputGain = 1.0f;
	};
}
