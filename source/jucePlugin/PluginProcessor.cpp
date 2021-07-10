#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "../synthLib/os.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                       )
	, m_device(synthLib::findROM())
	, m_plugin(&m_device)
{
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

	m_plugin.process(inputs, outputs, buffer.getNumSamples());

	m_plugin.getMidiOut(m_midiOut);

	for(size_t i=0; i<m_midiOut.size(); ++i)
	{
		const auto& e = m_midiOut[i];
		if(e.sysex.empty())
		{
			midiMessages.addEvent(juce::MidiMessage (e.a, e.b, e.c, 0.0), 0);
		}
		else
		{
			midiMessages.addEvent(juce::MidiMessage (&e.sysex[0], static_cast<int>(e.sysex.size()), 0.0), 0);
		}
	}
	m_midiOut.clear();
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
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
