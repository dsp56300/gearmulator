#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "version.h"

#include "ui/VirusEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_btSingleMode("Single Mode"), m_btMultiMode("Multi Mode"),
	m_parameterBinding(p),
	m_btLoadFile("Load bank"), m_cmbMidiInput("Midi Input"), m_cmbMidiOutput("Midi Output"),
	m_tempEditor(p)
{
    ignoreUnused (processorRef);

	juce::PropertiesFile::Options opts;
	opts.applicationName = "DSP56300 Emulator";
	opts.filenameSuffix = ".settings";
	opts.folderName = "DSP56300 Emulator";
	opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
	m_properties = new juce::PropertiesFile(opts);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
	setSize(800, 800);

	// Resizable UI
	setResizable(true, true);
	setResizeLimits(800,400,800,1600);

	m_btSingleMode.setRadioGroupId(0x3cf);
	m_btMultiMode.setRadioGroupId(0x3cf);
	addAndMakeVisible(m_btSingleMode);
	addAndMakeVisible(m_btMultiMode);
	m_btSingleMode.setTopLeftPosition(0,0);
	m_btSingleMode.setSize(120,30);
	m_btMultiMode.getToggleStateValue().referTo(*processorRef.getController().getParamValue(0, 2, 0x7a));
	const auto isMulti = processorRef.getController().isMultiMode();
	m_btSingleMode.setToggleState(!isMulti, juce::dontSendNotification);
	m_btMultiMode.setToggleState(isMulti, juce::dontSendNotification);
	m_btSingleMode.setClickingTogglesState(true);
	m_btMultiMode.setClickingTogglesState(true);
	m_btMultiMode.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + 10, m_btSingleMode.getY());
	m_btMultiMode.setSize(120,30);

	addAndMakeVisible(m_btLoadFile);
	m_btLoadFile.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + m_btMultiMode.getWidth() + 10, m_btSingleMode.getY());
	m_btLoadFile.setSize(120, 30);
	m_btLoadFile.onClick = [this]() {
			loadFile();
	};

	for (auto pt = 0; pt < 16; pt++)
	{
		m_partSelectors[pt].onClick = [this, pt]() {

			juce::PopupMenu selector;

			for(auto b=0; b<processorRef.getController().getBankCount(); ++b)
			{
				auto bank = processorRef.getController().getSinglePresetNames(b);
				juce::PopupMenu p;
				for (auto i = 0; i < 128; i++)
				{
					p.addItem(bank[i], [this, b, i, pt] { processorRef.getController().setCurrentPartPreset(pt, b, i); });
				}
				std::stringstream bankName;
				bankName << "Bank " << static_cast<char>('A' + b);
				selector.addSubMenu(std::string(bankName.str()), p);
			}
			selector.showMenu(juce::PopupMenu::Options());
		};
		m_partSelectors[pt].setSize(m_partSelectors[pt].getWidth() - 48, m_partSelectors[pt].getHeight());
		m_partSelectors[pt].setTopLeftPosition(m_partSelectors[pt].getPosition() + juce::Point(24, 0));
		addAndMakeVisible(m_partSelectors[pt]);

		m_prevPatch[pt].setSize(24, m_partSelectors[pt].getHeight());
		m_nextPatch[pt].setSize(24, m_partSelectors[pt].getHeight());
		m_prevPatch[pt].setTopLeftPosition(m_partSelectors[pt].getPosition() - juce::Point(24, 0));
		m_nextPatch[pt].setTopLeftPosition(m_partSelectors[pt].getPosition() + juce::Point(m_partSelectors[pt].getWidth(), 0));
		m_prevPatch[pt].setButtonText("<");
		m_nextPatch[pt].setButtonText(">");
		m_prevPatch[pt].onClick = [this, pt]() {
			processorRef.getController().setCurrentPartPreset(pt, processorRef.getController().getCurrentPartBank(pt),
															  std::max(0, processorRef.getController().getCurrentPartProgram(pt) - 1));
		};
		m_nextPatch[pt].onClick = [this, pt]() {
			processorRef.getController().setCurrentPartPreset(pt, processorRef.getController().getCurrentPartBank(pt),
															  std::min(127, processorRef.getController().getCurrentPartProgram(pt) + 1));
		};
		addAndMakeVisible(m_prevPatch[pt]);
		addAndMakeVisible(m_nextPatch[pt]);
	}
	
	auto midiIn = m_properties->getValue("midi_input", "");
	auto midiOut = m_properties->getValue("midi_output", "");
	if (midiIn != "")
	{
		processorRef.setMidiInput(midiIn);
	}
	if (midiOut != "")
	{
		processorRef.setMidiOutput(midiOut);
	}

	m_cmbMidiInput.setSize(160, 30);
	m_cmbMidiInput.setTopLeftPosition(0, 400);
	m_cmbMidiOutput.setSize(160, 30);
	m_cmbMidiOutput.setTopLeftPosition(164, 400);
	addAndMakeVisible(m_cmbMidiInput);
	addAndMakeVisible(m_cmbMidiOutput);
	m_cmbMidiInput.setTextWhenNoChoicesAvailable("No MIDI Inputs Enabled");
	auto midiInputs = juce::MidiInput::getAvailableDevices();
	juce::StringArray midiInputNames;
	midiInputNames.add(" - Midi In - ");
	auto inIndex = 0;
	for (int i = 0; i < midiInputs.size(); i++)
	{
		const auto input = midiInputs[i];
		if (processorRef.getMidiInput() != nullptr && input.identifier == processorRef.getMidiInput()->getIdentifier())
		{
			inIndex = i + 1;
		}
		midiInputNames.add(input.name);
	}
	m_cmbMidiInput.addItemList(midiInputNames, 1);
	m_cmbMidiInput.setSelectedItemIndex(inIndex, juce::dontSendNotification);
	m_cmbMidiOutput.setTextWhenNoChoicesAvailable("No MIDI Outputs Enabled");
	auto midiOutputs = juce::MidiOutput::getAvailableDevices();
	juce::StringArray midiOutputNames;
	midiOutputNames.add(" - Midi Out - ");
	auto outIndex = 0;
	for (int i = 0; i < midiOutputs.size(); i++)
	{
		const auto output = midiOutputs[i];
		if (processorRef.getMidiOutput() != nullptr && output.identifier == processorRef.getMidiOutput()->getIdentifier())
		{
			outIndex = i+1;
		}
		midiOutputNames.add(output.name);
	}
	m_cmbMidiOutput.addItemList(midiOutputNames, 1);
	m_cmbMidiOutput.setSelectedItemIndex(outIndex, juce::dontSendNotification);
	m_cmbMidiInput.onChange = [this]() { updateMidiInput(m_cmbMidiInput.getSelectedItemIndex()); };
	m_cmbMidiOutput.onChange = [this]() { updateMidiOutput(m_cmbMidiOutput.getSelectedItemIndex()); };

	addAndMakeVisible(m_tempEditor);

	startTimerHz(5);

	m_openEditor.setButtonText("Show Editor");
	m_openEditor.setTopLeftPosition(0, 500);
	m_openEditor.onClick = [this]()
	{
		m_virusEditor.reset(new juce::ResizableWindow("VirusEditor", true));
		m_virusEditor->setTopLeftPosition(0, 0);
		m_virusEditor->setUsingNativeTitleBar(true);
		m_virusEditor->setVisible(true);
		m_virusEditor->setResizable(true, false);
		m_virusEditor->setContentOwned(new VirusEditor(m_parameterBinding), true);
	};
	addAndMakeVisible(m_openEditor);
}
void AudioPluginAudioProcessorEditor::updateMidiInput(int index)
{ 
	auto list = juce::MidiInput::getAvailableDevices();

	if (index == 0)
	{
		m_properties->setValue("midi_input", "");
		m_properties->save();
		m_lastInputIndex = index;
		m_cmbMidiInput.setSelectedItemIndex(index, juce::dontSendNotification);
		return;
	}
	index--;
	auto newInput = list[index];

	if (!deviceManager.isMidiInputDeviceEnabled(newInput.identifier))
		deviceManager.setMidiInputDeviceEnabled(newInput.identifier, true);

	if (!processorRef.setMidiInput(newInput.identifier))
	{
		m_cmbMidiInput.setSelectedItemIndex(0, juce::dontSendNotification);
		m_lastInputIndex = 0;
		return;
	}

	m_properties->setValue("midi_input", newInput.identifier);
	m_properties->save();

	m_cmbMidiInput.setSelectedItemIndex(index+1, juce::dontSendNotification);
	m_lastInputIndex = index;
}
void AudioPluginAudioProcessorEditor::updateMidiOutput(int index)
{
	auto list = juce::MidiOutput::getAvailableDevices();

	if (index == 0)
	{
		m_properties->setValue("midi_output", "");
		m_properties->save();
		m_cmbMidiOutput.setSelectedItemIndex(index, juce::dontSendNotification);
		m_lastOutputIndex = index;
		processorRef.setMidiOutput("");
		return;
	}
	index--;
	auto newOutput = list[index];
	if(!processorRef.setMidiOutput(newOutput.identifier))
	{
		m_cmbMidiOutput.setSelectedItemIndex(0, juce::dontSendNotification);
		m_lastOutputIndex = 0;
		return;
	}
	m_properties->setValue("midi_output", newOutput.identifier);
	m_properties->save();

	m_cmbMidiOutput.setSelectedItemIndex(index+1, juce::dontSendNotification);
	m_lastOutputIndex = index;
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);

	std::string message = "DSP 56300 Emulator\nVersion " + std::string(g_pluginVersionString) + "\n" __DATE__ " " __TIME__;

	if(!processorRef.isPluginValid())
		message += "\n\nNo ROM, no sound!\nCopy ROM next to plugin, must end with .bin";

	g.drawFittedText(message, getLocalBounds().removeFromLeft(400).removeFromBottom(45), juce::Justification::centred,
					 2);
	g.drawFittedText("To donate: paypal.me/dsp56300", getLocalBounds().removeFromRight(400).removeFromTop(35),
					 juce::Justification::centred, 2);
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
	// ugly (polling!) way for refreshing presets names as this is temporary ui
	const auto multiMode = processorRef.getController().isMultiMode();
	for (auto pt = 0; pt < 16; pt++)
	{
		bool singlePartOrInMulti = pt == 0 || multiMode;
		m_partSelectors[pt].setVisible(singlePartOrInMulti);
		m_prevPatch[pt].setVisible(singlePartOrInMulti);
		m_nextPatch[pt].setVisible(singlePartOrInMulti);
		if (singlePartOrInMulti)
			m_partSelectors[pt].setButtonText(processorRef.getController().getCurrentPartPresetName(pt));
	}
}

void AudioPluginAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
	auto area = getLocalBounds();
	area.removeFromTop(35);
	m_tempEditor.setBounds(area.removeFromRight(400));
	for (auto pt = 0; pt < 16; pt++)
	{
		m_partSelectors[pt].setBounds(area.removeFromTop(20));
	}

	m_openEditor.setBounds(0, 0, 80, 20);
}

void AudioPluginAudioProcessorEditor::loadFile() {
	juce::FileChooser chooser("Choose syx/midi banks to import",
		m_previousPath.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : m_previousPath,
							  "*.syx,*.mid,*.midi",
							  true);
	const bool result = chooser.browseForFileToOpen();
	if (result)
	{
		const auto result = chooser.getResult();
		m_previousPath = result.getParentDirectory().getFullPathName();
		const auto ext = result.getFileExtension().toLowerCase();
		if (ext == ".syx")
		{
			juce::MemoryBlock data;
			result.loadFileAsData(data);
			for (auto it = data.begin(); it != data.end(); it += 267)
			{
				if ((it + 267) < data.end())
				{
					processorRef.getController().parseMessage(Virus::SysEx(it, it + 267));
				}
			}
			m_btLoadFile.setButtonText("Loaded");
		}
		else if (ext == ".mid" || ext == ".midi")
		{
			juce::MemoryBlock data;
			if (!result.loadFileAsData(data))
			{
				return;
			}
			const uint8_t *ptr = (uint8_t *)data.getData();
			const auto end = ptr + data.getSize();

			for (auto it = ptr; it < end; it += 1)
			{
				if ((uint8_t)*it == (uint8_t)0xf0 && (it+267) < end)
				{
					if ((uint8_t) *(it + 1) == (uint8_t)0x00)
					{
						auto syx = Virus::SysEx(it, it + 267);
						syx[7] = 0x01; // force to bank a
						syx[266] = 0xf7;
						processorRef.getController().parseMessage(syx);
						
						it += 266;
					}
					else // some midi files have two bytes after the 0xf0
					{
						auto syx = Virus::SysEx();
						syx.push_back(0xf0);
						for (auto i = it + 3; i < it + 3 + 266; i++)
						{
								syx.push_back((uint8_t)*i);
						}
						syx[7] = 0x01; // force to bank a
						syx[266] = 0xf7;
						processorRef.getController().parseMessage(syx);
						it += 266;
					}
				}
			}
		}
	}	
}