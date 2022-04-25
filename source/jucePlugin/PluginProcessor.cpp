#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../synthLib/os.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
    AudioProcessor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 1", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("USB 3", juce::AudioChannelSet::stereo(), true)
#endif
	),
	MidiInputCallback(),
	m_romName(virusLib::ROMFile::findROM()),
	m_rom(m_romName),
	m_device(m_rom), m_plugin(&m_device)
{
	getController(); // init controller
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() = default;

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

	if constexpr(JucePlugin_IsSynth)
		setLatencySamples(m_plugin.getLatencyMidiToOutput());
	else
		setLatencySamples(m_plugin.getLatencyInputToOutput());
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

    // This checks if the input is stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
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

    synthLib::TAudioInputs inputs{};
    synthLib::TAudioOutputs outputs{};

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    	inputs[channel] = buffer.getReadPointer(channel);

    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    	outputs[channel] = buffer.getWritePointer(channel);

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

			const auto status = ev.a & 0xf0;

			if(status == synthLib::M_CONTROLCHANGE || status == synthLib::M_POLYPRESSURE)
			{
				// forward to UI to react to control input changes that should move knobs
				getController().dispatchVirusOut(std::vector<synthLib::SMidiEvent>{ev});
			}
		}

		ev.offset = metadata.samplePosition;

		m_plugin.addMidiEvent(ev);
	}

	midiMessages.clear();

    juce::AudioPlayHead::CurrentPositionInfo pos{};

	auto* playHead = getPlayHead();
	if(playHead) {
		playHead->getCurrentPosition(pos);

		if(pos.bpm > 0) { // sync virus interal clock to host
			const uint8_t bpmValue = juce::jmin(127, juce::jmax(0, (int)pos.bpm-63)); // clamp to virus range, 63-190
			auto clockParam = getController().getParameter(Virus::Param_ClockTempo, 0);
			if (clockParam != nullptr && (int)clockParam->getValueObject().getValue() != bpmValue) {
				clockParam->getValueObject().setValue(bpmValue);
			}
		}
	}

    m_plugin.process(inputs, outputs, buffer.getNumSamples(), static_cast<float>(pos.bpm),
                     static_cast<float>(pos.ppqPosition), pos.isPlaying);

    m_midiOut.clear();
    m_plugin.getMidiOut(m_midiOut);

    if (!m_midiOut.empty())
	{
		getController().dispatchVirusOut(m_midiOut);
	}

    for (auto& e : m_midiOut)
    {
	    if (e.source == synthLib::MidiEventSourceEditor)
			continue;

    	if (e.sysex.empty())
		{
			const juce::MidiMessage message(e.a, e.b, e.c, 0.0);
			midiMessages.addEvent(message, 0);

			// additionally send to the midi output we've selected in the editor
			if (m_midiOutput)
				m_midiOutput->sendMessageNow(message);
		}
		else
		{
			const juce::MidiMessage message(&e.sysex[0], static_cast<int>(e.sysex.size()), 0.0);
			midiMessages.addEvent(message, 0);

			// additionally send to the midi output we've selected in the editor
			if (m_midiOutput)
				m_midiOutput->sendMessageNow(message);
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

juce::MidiOutput *AudioPluginAudioProcessor::getMidiOutput() const { return m_midiOutput.get(); }
juce::MidiInput *AudioPluginAudioProcessor::getMidiInput() const { return m_midiInput.get(); }

bool AudioPluginAudioProcessor::setMidiOutput(const juce::String& _out)
{
	if (m_midiOutput != nullptr && m_midiOutput->isBackgroundThreadRunning())
	{
		m_midiOutput->stopBackgroundThread();
	}
	m_midiOutput = juce::MidiOutput::openDevice(_out);
	if (m_midiOutput != nullptr)
	{
		m_midiOutput->startBackgroundThread();
		return true;
	}
	return false;
}

bool AudioPluginAudioProcessor::setMidiInput(const juce::String& _in)
{
	if (m_midiInput != nullptr)
	{
		m_midiInput->stop();
	}
	m_midiInput = juce::MidiInput::openDevice(_in, this);
	if (m_midiInput != nullptr)
	{
		m_midiInput->start();
		return true;
	}
	return false;
}

void AudioPluginAudioProcessor::handleIncomingMidiMessage(juce::MidiInput *source, const juce::MidiMessage &message)
{
	const auto* raw = message.getSysExData();
	if (raw)
	{
		const auto count = message.getSysExDataSize();
		auto syx = Virus::SysEx();
		syx.push_back(0xf0);
		for (int i = 0; i < count; i++)
		{
			syx.push_back(raw[i]);
		}
		syx.push_back(0xf7);
		synthLib::SMidiEvent sm;
		sm.source = synthLib::MidiEventSourcePlugin;
		sm.sysex = syx;
		getController().parseMessage(syx);

		addMidiEvent(sm);

		if (m_midiOutput)
		{
			std::vector<synthLib::SMidiEvent> data;
			getLastMidiOut(data);
			if (!data.empty())
			{
				const auto msg = juce::MidiMessage::createSysExMessage(data.data(), static_cast<int>(data.size()));

				m_midiOutput->sendMessageNow(msg);
			}
		}
	}
	else
	{
		const auto count = message.getRawDataSize();
		const auto* rawData = message.getRawData();
		if (count >= 1 && count <= 3)
		{
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			sm.a = rawData[0];
			sm.b = count > 1 ? rawData[1] : 0;
			sm.c = count > 2 ? rawData[2] : 0;
			addMidiEvent(sm);
		}
		else
		{
			synthLib::SMidiEvent sm;
			sm.source = synthLib::MidiEventSourcePlugin;
			auto syx = Virus::SysEx();
			for (int i = 0; i < count; i++)
			{
				syx.push_back(rawData[i]);
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
	if (m_controller)
		m_controller->onStateLoaded();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
