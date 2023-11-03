#include "PluginProcessor.h"
#include "PluginEditorState.h"
#include "ParameterNames.h"

#include "../jucePluginEditorLib/pluginEditorWindow.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

static juce::PropertiesFile::Options getConfigOptions()
{
	juce::PropertiesFile::Options opts;
	opts.applicationName = "DSP56300 Emulator";
	opts.filenameSuffix = ".settings";
	opts.folderName = "DSP56300 Emulator";
	opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
	return opts;
}

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor() :
    jucePluginEditorLib::Processor(BusesProperties()
                   .withInput("Input", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#if JucePlugin_IsSynth
                   .withOutput("Out 2", juce::AudioChannelSet::stereo(), true)
                   .withOutput("Out 3", juce::AudioChannelSet::stereo(), true)
#endif
	, ::getConfigOptions())
{
	m_clockTempoParam = getController().getParameterIndexByName(Virus::g_paramClockTempo);

	const auto latencyBlocks = getConfig().getIntValue("latencyBlocks", static_cast<int>(getPlugin().getLatencyBlocks()));
	setLatencyBlocks(latencyBlocks);
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
				static_cast<Virus::Controller&>(getController()).addPluginMidiOut(std::vector{ev});
			}
		}

		ev.offset = metadata.samplePosition;

		getPlugin().addMidiEvent(ev);
	}

	midiMessages.clear();

    juce::AudioPlayHead::CurrentPositionInfo pos{};

	auto* playHead = getPlayHead();
	if(playHead) {
		playHead->getCurrentPosition(pos);

		if(pos.bpm > 0) { // sync virus interal clock to host
			const uint8_t bpmValue = juce::jmin(127, juce::jmax(0, (int)pos.bpm-63)); // clamp to virus range, 63-190
			const auto clockParam = getController().getParameter(m_clockTempoParam, 0);
			if (clockParam != nullptr && (int)clockParam->getValueObject().getValue() != bpmValue) {
				clockParam->getValueObject().setValue(bpmValue);
			}
		}
	}

	getPlugin().process(inputs, outputs, buffer.getNumSamples(), static_cast<float>(pos.bpm),
                     static_cast<float>(pos.ppqPosition), pos.isPlaying);

    m_midiOut.clear();
    getPlugin().getMidiOut(m_midiOut);

    if (!m_midiOut.empty())
	{
		getController().addPluginMidiOut(m_midiOut);
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

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
	if(!m_editorState)
		m_editorState.reset(new PluginEditorState(*this, getController()));
    return new jucePluginEditorLib::EditorWindow(*this, *m_editorState, getConfig());
}

void AudioPluginAudioProcessor::updateLatencySamples()
{
	if constexpr(JucePlugin_IsSynth)
		setLatencySamples(getPlugin().getLatencyMidiToOutput());
	else
		setLatencySamples(getPlugin().getLatencyInputToOutput());
}

synthLib::Device* AudioPluginAudioProcessor::createDevice()
{
	m_rom.reset(new virusLib::ROMFile(std::string()));
	return new virusLib::Device(*m_rom);
}

pluginLib::Controller* AudioPluginAudioProcessor::createController()
{
	// force creation of device as the controller decides how to initialize based on the used ROM
	getPlugin();

	return new Virus::Controller(*this);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
