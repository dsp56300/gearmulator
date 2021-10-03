#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "version.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_btSingleMode("Single Mode"), m_btMultiMode("Multi Mode"),
	m_btLoadFile("Load bank"),
	m_tempEditor(p)
{
    ignoreUnused (processorRef);

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
	m_btMultiMode.getToggleStateValue().referTo(*processorRef.getController().getParam(0, 2, 0x7a));
	const auto isMulti = processorRef.getController().isMultiMode();
	m_btSingleMode.setToggleState(!isMulti, juce::dontSendNotification);
	m_btMultiMode.setToggleState(isMulti, juce::dontSendNotification);
	m_btSingleMode.setClickingTogglesState(true);
	m_btMultiMode.setClickingTogglesState(true);
	m_btMultiMode.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + 10, 0);
	m_btMultiMode.setSize(120,30);

	addAndMakeVisible(m_btLoadFile);
	m_btLoadFile.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + m_btMultiMode.getWidth() + 10, 0);
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
		addAndMakeVisible(m_partSelectors[pt]);
	}

	addAndMakeVisible(m_tempEditor);

	startTimerHz(5);
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