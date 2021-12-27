#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../synthLib/os.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
    AudioProcessor(BusesProperties()
                       .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	m_device(synthLib::findROM()), m_plugin(&m_device), m_midiOutput(), m_midiInput(), juce::MidiInputCallback()
{
	auto &ctrl = getController(); // init controller
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return "default";
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
	m_plugin.setSamplerate(static_cast<float>(sampleRate));
	m_plugin.setBlockSize(samplesPerBlock);
	setLatencySamples(m_plugin.getLatencySamples());
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
		buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.

	float* inputs[8] = {};
	float* outputs[8] = {};

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
	    const float* in = buffer.getReadPointer(channel);
	    float* out = buffer.getWritePointer(channel);

    	inputs[channel] = const_cast<float*>(in);	// TODO: fixme
    	outputs[channel] = out;
    }

	for(const auto metadata : midiMessages)
	{
		const auto message = metadata.getMessage();

		synthLib::SMidiEvent ev{};

		if(message.isSysEx() || message.getRawDataSize() > 3)
		{
			ev.sysex.resize(message.getRawDataSize());
			memcpy( &ev.sysex[0], message.getRawData(), ev.sysex.size());

			// Juce bug? Or VSTHost bug? Juce inserts f0/f7 when converting VST3 midi packet to Juce packet, but its already there
			if(ev.sysex.size() > 1)
			{
				if(ev.sysex.front() == 0xf0 && ev.sysex[1] == 0xf0)
					ev.sysex.erase(ev.sysex.begin());

				if(ev.sysex.size() > 1)
				{
					if(ev.sysex[ev.sysex.size()-1] == 0xf7 && ev.sysex[ev.sysex.size()-2] == 0xf7)
						ev.sysex.erase(ev.sysex.begin());
				}
			}
		}
		else
		{
			ev.a = message.getRawData()[0];
			ev.b = message.getRawDataSize() > 0 ? message.getRawData()[1] : 0;
			ev.c = message.getRawDataSize() > 1 ? message.getRawData()[2] : 0;
		}

		ev.offset = metadata.samplePosition;

		m_plugin.addMidiEvent(ev);
	}

	midiMessages.clear();

    juce::AudioPlayHead::CurrentPositionInfo pos{};

	auto* playHead = getPlayHead();
	if(playHead)
		playHead->getCurrentPosition(pos);

	m_plugin.process(inputs, outputs, buffer.getNumSamples(), static_cast<float>(pos.bpm), static_cast<float>(pos.ppqPosition), pos.isPlaying);

	m_midiOut.clear();
	m_plugin.getMidiOut(m_midiOut);

    if (!m_midiOut.empty())
	{
		m_controller->dispatchVirusOut(m_midiOut);
	}

    for (size_t i = 0; i < m_midiOut.size(); ++i)
    {
        const auto& e = m_midiOut[i];

		if (e.source == synthLib::MidiEventSourceEditor)
			continue;

    	if (e.sysex.empty())
		{

			midiMessages.addEvent(juce::MidiMessage(e.a, e.b, e.c, 0.0), 0);

			// additionally send to the midi output we've selected in the editor
			if (m_midiOutput != nullptr)
			{
				m_midiOutput.get()->sendMessageNow(juce::MidiMessage(e.a, e.b, e.c, 0.0));
			}
		}
		else
		{

			// additionally send to the midi output we've selected in the editor
			if (m_midiOutput != nullptr)
			{
				m_midiOutput.get()->sendMessageNow(
					juce::MidiMessage(&e.sysex[0], static_cast<int>(e.sysex.size()), 0.0));
			}
			midiMessages.addEvent(juce::MidiMessage(&e.sysex[0], static_cast<int>(e.sysex.size()), 0.0), 0);
		}


    }
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

	std::vector<uint8_t> state;
	m_plugin.getState(state, synthLib::StateTypeGlobal);
	destData.append(&state[0], state.size());
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
	setState(data, sizeInBytes);
}

void AudioPluginAudioProcessor::getCurrentProgramStateInformation(juce::MemoryBlock& destData)
{
	std::vector<uint8_t> state;
	m_plugin.getState(state, synthLib::StateTypeCurrentProgram);
	destData.append(&state[0], state.size());
}

void AudioPluginAudioProcessor::setCurrentProgramStateInformation(const void* data, int sizeInBytes)
{
	setState(data, sizeInBytes);
}

void AudioPluginAudioProcessor::getLastMidiOut(std::vector<synthLib::SMidiEvent>& dst)
{
	juce::ScopedLock lock(getCallbackLock());
	std::swap(dst, m_midiOut);
	m_midiOut.clear();
}

void AudioPluginAudioProcessor::addMidiEvent(const synthLib::SMidiEvent& ev)
{
	m_plugin.addMidiEvent(ev);
}

juce::MidiOutput *AudioPluginAudioProcessor::getMidiOutput() { return m_midiOutput.get(); }
juce::MidiInput *AudioPluginAudioProcessor::getMidiInput() { return m_midiInput.get(); }

bool AudioPluginAudioProcessor::setMidiOutput(juce::String _out) {
	if (m_midiOutput != nullptr && m_midiOutput->isBackgroundThreadRunning())
	{
		m_midiOutput->stopBackgroundThread();
	}
	m_midiOutput.swap(juce::MidiOutput::openDevice(_out));
	if (m_midiOutput != nullptr)
	{
		m_midiOutput->startBackgroundThread();
		return true;
	}
	return false;
}

bool AudioPluginAudioProcessor::setMidiInput(juce::String _in)
{
	if (m_midiInput != nullptr)
	{
		m_midiInput->stop();
	}
	m_midiInput.swap(juce::MidiInput::openDevice(_in, this));
	if (m_midiInput != nullptr)
	{
		m_midiInput->start();
		return true;
	}
	return false;
}

void AudioPluginAudioProcessor::handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message)
{
	auto raw = message.getSysExData();
	if (raw != 0)
	{
		auto count = message.getSysExDataSize();
		auto syx = Virus::SysEx();
		syx.push_back((uint8_t)0xf0);
		for (int i = 0; i < count; i++)
		{
			syx.push_back((uint8_t)raw[i]);
		}
		syx.push_back((uint8_t)0xf7);
		synthLib::SMidiEvent sm;
		sm.source = synthLib::MidiEventSourcePlugin;
		sm.sysex = syx;
		getController().parseMessage(syx);


		addMidiEvent(sm);
		if (m_midiOutput != nullptr && m_midiOutput.get() != 0)
		{
			std::vector<synthLib::SMidiEvent> data;
			getLastMidiOut(data);
			if (data.size() > 0)
			{
				auto msg = juce::MidiMessage::createSysExMessage(data.data(), data.size());

				m_midiOutput->sendMessageNow(msg);
			}
		}
	}
	else
	{
		auto count = message.getRawDataSize();
		auto raw = message.getRawData();
		if (count >= 1 && count <= 3)
		{
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			sm.a = raw[0];
			sm.b = count > 1 ? raw[1] : 0;
			sm.c = count > 2 ? raw[2] : 0;
			addMidiEvent(sm);
		}
		else
		{
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			auto syx = Virus::SysEx();
			for (int i = 0; i < count; i++)
			{
				syx.push_back((uint8_t)raw[i]);
			}
			sm.sysex = syx;
			addMidiEvent(sm);
		}
	}
}

Virus::Controller &AudioPluginAudioProcessor::getController()
{
    if (m_controller == nullptr)
    {
        // initialize controller if not exists.
        // assures PluginProcessor is fully constructed!
        m_controller = std::make_unique<Virus::Controller>(*this);
    }
    return *m_controller;
}

void AudioPluginAudioProcessor::setState(const void* _data, size_t _sizeInBytes)
{
	if(_sizeInBytes < 1)
		return;

	std::vector<uint8_t> state;
	state.resize(_sizeInBytes);
	memcpy(&state[0], _data, _sizeInBytes);
	m_plugin.setState(state);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
