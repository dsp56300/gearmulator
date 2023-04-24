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

		virtual synthLib::Plugin& getPlugin() = 0;
		bool hasController() const
		{
			return m_controller.get();
		}

		virtual bool setLatencyBlocks(uint32_t _blocks);
		virtual void updateLatencySamples() = 0;

	private:
		void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
		void releaseResources() override;

		//==============================================================================
	    void getStateInformation (juce::MemoryBlock& destData) override;
	    void setStateInformation (const void* data, int sizeInBytes) override;
	    void getCurrentProgramStateInformation (juce::MemoryBlock& destData) override;
	    void setCurrentProgramStateInformation (const void* data, int sizeInBytes) override;

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

	protected:
		std::unique_ptr<juce::MidiOutput> m_midiOutput{};
		std::unique_ptr<juce::MidiInput> m_midiInput{};
		std::vector<synthLib::SMidiEvent> m_midiOut{};
	};
}
