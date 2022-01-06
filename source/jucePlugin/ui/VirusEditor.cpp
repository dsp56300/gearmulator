#include "VirusEditor.h"
#include "BinaryData.h"
#include "../version.h"
#include "Virus_ArpEditor.h"
#include "Virus_FxEditor.h"
#include "Virus_LfoEditor.h"
#include "Virus_OscEditor.h"
#include "../VirusParameterBinding.h"
#include "../VirusController.h"

using namespace juce;

constexpr auto kPanelWidth = 1377;
constexpr auto kPanelHeight = 800;

VirusEditor::VirusEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef) :
    m_parameterBinding(_parameterBinding), processorRef(_processorRef), m_controller(processorRef.getController()), m_btSingleMode("Single Mode"), m_btMultiMode("Multi Mode")
{
    setLookAndFeel(&m_lookAndFeel);

    m_background = Drawable::createFromImageData (BinaryData::bg_1377x800_png, BinaryData::bg_1377x800_pngSize);

    m_background->setBufferedToImage (true);
    addAndMakeVisible (*m_background);
    addAndMakeVisible (m_mainButtons);

    m_arpEditor = std::make_unique<ArpEditor>(_parameterBinding);
    m_fxEditor = std::make_unique<FxEditor>(_parameterBinding);
    m_lfoEditor = std::make_unique<LfoEditor>(_parameterBinding);
    m_oscEditor = std::make_unique<OscEditor>(_parameterBinding);

    applyToSections([this](Component *s) { addChildComponent(s); });

    // show/hide section from buttons..
    m_mainButtons.updateSection = [this]() {
        m_arpEditor->setVisible(m_mainButtons.m_arpSettings.getToggleState());
        m_fxEditor->setVisible(m_mainButtons.m_effects.getToggleState());
        m_lfoEditor->setVisible(m_mainButtons.m_lfoMatrix.getToggleState());
        m_oscEditor->setVisible(m_mainButtons.m_oscFilter.getToggleState());
    };
    m_oscEditor->setVisible(true);
    m_mainButtons.m_oscFilter.setToggleState(true, NotificationType::dontSendNotification);
    addAndMakeVisible(m_presetButtons);
    m_presetButtons.m_load.onClick = [this]() { loadFile(); };
    for (auto pt = 0; pt < 16; pt++)
    {
        m_partLabels[pt].setBounds(34, 161 + pt * (36), 39, 36);
        m_partLabels[pt].setText(juce::String(pt + 1), juce::dontSendNotification);
        m_partLabels[pt].setColour(0, juce::Colours::white);
        m_partLabels[pt].setColour(1, juce::Colour(45, 24, 24));
        m_partLabels[pt].setJustificationType(Justification::centredLeft);
        addAndMakeVisible(m_partLabels[pt]);

        m_partSelect[pt].setBounds(34, 161 + pt*(36), 39, 36);
        m_partSelect[pt].setButtonText(juce::String(pt));
        m_partSelect[pt].setRadioGroupId(kPartGroupId);
        m_partSelect[pt].setClickingTogglesState(true);
        m_partSelect[pt].onClick = [this, pt]() {
            this->changePart(pt);
        };
        addAndMakeVisible(m_partSelect[pt]);
        
        m_presetNames[pt].setBounds(80, 172 + pt * (36), 136, 16);
        m_presetNames[pt].setButtonText(m_controller.getCurrentPartPresetName(pt));
        m_presetNames[pt].setColour(0, juce::Colours::white);

        m_presetNames[pt].onClick = [this, pt]() {
            juce::PopupMenu selector;

            for (uint8_t b = 0; b < m_controller.getBankCount(); ++b)
            {
                const auto bank = virusLib::fromArrayIndex(b);
                auto presetNames = m_controller.getSinglePresetNames(bank);
                juce::PopupMenu p;
                for (uint8_t j = 0; j < 128; j++)
                {
                    const auto presetName = presetNames[j];
                    p.addItem(presetNames[j], [this, bank, j, pt, presetName] {
                        m_controller.setCurrentPartPreset(pt, bank, j);
                        m_presetNames[pt].setButtonText(presetName);
                    });
                }
                std::stringstream bankName;
                bankName << "Bank " << static_cast<char>('A' + b);
                selector.addSubMenu(std::string(bankName.str()), p);
            }
            selector.showMenu(juce::PopupMenu::Options());
        };
        addAndMakeVisible(m_presetNames[pt]);

        m_prevPatch[pt].setBounds(228, 173 + 36*pt, 16, 12);
        m_nextPatch[pt].setBounds(247, 173 + 36*pt, 16, 12);
        m_prevPatch[pt].setButtonText("<");
        m_nextPatch[pt].setButtonText(">");
        m_prevPatch[pt].onClick = [this, pt]() {
            m_controller.setCurrentPartPreset(
                pt, m_controller.getCurrentPartBank(pt),
                std::max(0, m_controller.getCurrentPartProgram(pt) - 1));
        };
        m_nextPatch[pt].onClick = [this, pt]() {
            m_controller.setCurrentPartPreset(
                pt, m_controller.getCurrentPartBank(pt),
                std::min(127, m_controller.getCurrentPartProgram(pt) + 1));
        };
        addAndMakeVisible(m_prevPatch[pt]);
        addAndMakeVisible(m_nextPatch[pt]);
    }
    m_partSelect[0].setToggleState(true, NotificationType::sendNotification);

    m_btSingleMode.setRadioGroupId(0x3cf);
    m_btMultiMode.setRadioGroupId(0x3cf);
    addAndMakeVisible(m_btSingleMode);
    addAndMakeVisible(m_btMultiMode);
    m_btSingleMode.setTopLeftPosition(102, 756);
    m_btSingleMode.setSize(100, 30);
    m_btMultiMode.getToggleStateValue().referTo(*m_controller.getParamValue(Virus::Param_PlayMode));
    const auto isMulti = m_controller.isMultiMode();
    m_btSingleMode.setToggleState(!isMulti, juce::dontSendNotification);
    m_btMultiMode.setToggleState(isMulti, juce::dontSendNotification);
    m_btSingleMode.setClickingTogglesState(false);
    m_btMultiMode.setClickingTogglesState(false);
    m_btSingleMode.onClick = [this]() { setPlayMode(0); };
    m_btMultiMode.onClick = [this]() { setPlayMode(1); };

    m_btMultiMode.setTopLeftPosition(m_btSingleMode.getPosition().x + m_btSingleMode.getWidth() + 10,
    m_btSingleMode.getY());
    m_btMultiMode.setSize(100, 30);

    juce::PropertiesFile::Options opts;
    opts.applicationName = "DSP56300 Emulator";
    opts.filenameSuffix = ".settings";
    opts.folderName = "DSP56300 Emulator";
    opts.osxLibrarySubFolder = "Application Support/DSP56300 Emulator";
    m_properties = new juce::PropertiesFile(opts);
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
    m_cmbMidiInput.setTopLeftPosition(350, 760);
    m_cmbMidiOutput.setSize(160, 30);
    m_cmbMidiOutput.setTopLeftPosition(350+164, 760);
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
        if (processorRef.getMidiOutput() != nullptr &&
            output.identifier == processorRef.getMidiOutput()->getIdentifier())
        {
            outIndex = i + 1;
        }
        midiOutputNames.add(output.name);
    }
    m_cmbMidiOutput.addItemList(midiOutputNames, 1);
    m_cmbMidiOutput.setSelectedItemIndex(outIndex, juce::dontSendNotification);
    m_cmbMidiInput.onChange = [this]() { updateMidiInput(m_cmbMidiInput.getSelectedItemIndex()); };
    m_cmbMidiOutput.onChange = [this]() { updateMidiOutput(m_cmbMidiOutput.getSelectedItemIndex()); };

    std::string message =
        "DSP 56300 Emulator\nVersion " + std::string(g_pluginVersionString) + "\n" __DATE__ " " __TIME__;
    if (!processorRef.isPluginValid())
        message += "\n\nNo ROM, no sound!\nCopy ROM next to plugin, must end with .bin";
    message += "\n\nTo donate: paypal.me/dsp56300";
    m_version.setText(message, NotificationType::dontSendNotification);
    m_version.setBounds(94, 2, 220, 150);
    m_version.setColour(juce::Label::textColourId, juce::Colours::white);
    m_version.setJustificationType(Justification::centred);
    addAndMakeVisible(m_version);

    m_patchName.setBounds(410, 48, 362, 36);
    m_patchName.setJustificationType(Justification::left);
    m_patchName.setFont(juce::Font(32.0f, juce::Font::bold));
    m_patchName.setColour(juce::Label::textColourId, juce::Colours::red);
    addAndMakeVisible(m_patchName);

    startTimerHz(5);
    setSize (kPanelWidth, kPanelHeight);
}

VirusEditor::~VirusEditor() { setLookAndFeel(nullptr); }

void VirusEditor::timerCallback()
{
    // ugly (polling!) way for refreshing presets names as this is temporary ui
    const auto multiMode = m_controller.isMultiMode();
    for (auto pt = 0; pt < 16; pt++)
    {
        bool singlePartOrInMulti = pt == 0 || multiMode;
        m_presetNames[pt].setVisible(singlePartOrInMulti);
        m_prevPatch[pt].setVisible(singlePartOrInMulti);
        m_nextPatch[pt].setVisible(singlePartOrInMulti);
        if (singlePartOrInMulti)
            m_presetNames[pt].setButtonText(m_controller.getCurrentPartPresetName(pt));
        if (pt == m_parameterBinding.m_part)
        {
            m_patchName.setText(m_controller.getCurrentPartPresetName(pt),
                                NotificationType::dontSendNotification);
        }
    }
    
}

void VirusEditor::updateMidiInput(int index)
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

    m_cmbMidiInput.setSelectedItemIndex(index + 1, juce::dontSendNotification);
    m_lastInputIndex = index;
}
void VirusEditor::updateMidiOutput(int index)
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
    if (!processorRef.setMidiOutput(newOutput.identifier))
    {
        m_cmbMidiOutput.setSelectedItemIndex(0, juce::dontSendNotification);
        m_lastOutputIndex = 0;
        return;
    }
    m_properties->setValue("midi_output", newOutput.identifier);
    m_properties->save();

    m_cmbMidiOutput.setSelectedItemIndex(index + 1, juce::dontSendNotification);
    m_lastOutputIndex = index;
}
void VirusEditor::updatePartsPresetNames()
{
    for (auto i = 0; i < 16; i++)
    {
        m_presetNames[i].setButtonText(m_controller.getCurrentPartPresetName(i));
    }
}
void VirusEditor::applyToSections(std::function<void(Component *)> action)
{
    for (auto *section : {static_cast<Component *>(m_arpEditor.get()), static_cast<Component *>(m_fxEditor.get()),
                          static_cast<Component *>(m_lfoEditor.get()), static_cast<Component *>(m_oscEditor.get())})
    {
        action(section);
    }
}

void VirusEditor::resized()
{
    m_background->setBounds (getLocalBounds());
    m_mainButtons.setBounds (394, 106, m_mainButtons.getWidth(), m_mainButtons.getHeight());
    auto statusArea = Rectangle<int>(395, 36, 578, 60);
    m_presetButtons.setBounds(statusArea.removeFromRight(188));
    applyToSections([this](Component *s) { s->setTopLeftPosition(338, 133); });
}

void VirusEditor::setPlayMode(uint8_t _mode) {
    m_controller.getParameter(Virus::Param_PlayMode)->setValue(_mode);
    changePart(0);
}

void VirusEditor::changePart(uint8_t _part)
{
    for (auto &p : m_partSelect)
    {
        p.setToggleState(false, juce::dontSendNotification);
    }
    m_partSelect[_part].setToggleState(true, juce::dontSendNotification);
    m_parameterBinding.setPart(_part);

    removeChildComponent(m_oscEditor.get());
    removeChildComponent(m_lfoEditor.get());
    removeChildComponent(m_fxEditor.get());
    removeChildComponent(m_arpEditor.get());

    m_oscEditor	= std::make_unique<OscEditor>(m_parameterBinding);
    addChildComponent(m_oscEditor.get());

    m_lfoEditor = std::make_unique<LfoEditor>(m_parameterBinding);
    addChildComponent(m_lfoEditor.get());

    m_fxEditor = std::make_unique<FxEditor>(m_parameterBinding);
    addChildComponent(m_fxEditor.get());

    m_arpEditor = std::make_unique<ArpEditor>(m_parameterBinding);
    addChildComponent(m_arpEditor.get());

    m_mainButtons.updateSection();
    resized();
}

VirusEditor::MainButtons::MainButtons()
: m_oscFilter ("OSC|FILTER", DrawableButton::ImageRaw)
, m_lfoMatrix ("LFO|MATRIX", DrawableButton::ImageRaw)
, m_effects ("EFFECTS", DrawableButton::ImageRaw)
, m_arpSettings ("ARP|SETTINGS", DrawableButton::ImageRaw)
{
    constexpr auto numOfMainButtons = 4;
    setupButton (0, Drawable::createFromImageData (BinaryData::GLOBAL_btn_osc_filter_141x26_png, BinaryData::GLOBAL_btn_osc_filter_141x26_pngSize), m_oscFilter);
    setupButton (1, Drawable::createFromImageData (BinaryData::GLOBAL_btn_lfo_matrix_141x26_png, BinaryData::GLOBAL_btn_lfo_matrix_141x26_pngSize), m_lfoMatrix);
    setupButton (2, Drawable::createFromImageData (BinaryData::GLOBAL_btn_effects_141x26_png, BinaryData::GLOBAL_btn_effects_141x26_pngSize), m_effects);
    setupButton (3, Drawable::createFromImageData (BinaryData::GLOBAL_btn_arp_settings_141x26_png, BinaryData::GLOBAL_btn_arp_settings_141x26_pngSize), m_arpSettings);
    setSize ((kButtonWidth + kMargin) * numOfMainButtons, kButtonHeight);
}


void VirusEditor::MainButtons::valueChanged(juce::Value &) { updateSection(); }

void VirusEditor::MainButtons::setupButton (int i, std::unique_ptr<Drawable>&& btnImage, juce::DrawableButton& btn)
{
    auto onImage = btnImage->createCopy();
    onImage->setOriginWithOriginalSize({0, -kButtonHeight});
    btn.setClickingTogglesState (true);
    btn.setRadioGroupId (kGroupId);
    btn.setImages (btnImage.get(), nullptr, nullptr, nullptr, onImage.get());
    btn.setBounds ((i > 1 ? -1 : 0) + i * (kButtonWidth + kMargin), 0, kButtonWidth, kButtonHeight);
    btn.getToggleStateValue().addListener(this);
    addAndMakeVisible (btn);
}

VirusEditor::PresetButtons::PresetButtons()
{
    for (auto *btn : {&m_save, &m_load, &m_presets})
        addAndMakeVisible(btn);
    constexpr auto y = 8;
    constexpr auto w = Buttons::PresetButton::kWidth;
    m_save.setBounds(28, y, w, Buttons::PresetButton::kHeight);
    m_load.setBounds(36 + w, y, w, Buttons::PresetButton::kHeight);
    m_presets.setBounds(43 + w * 2, y, w, Buttons::PresetButton::kHeight);
}



VirusEditor::PartButtons::PartButtons()
{
}

void VirusEditor::loadFile()
{
    juce::FileChooser chooser(
        "Choose syx/midi banks to import",
        m_previousPath.isEmpty()
            ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory()
            : m_previousPath,
        "*.syx,*.mid,*.midi", true);

    if (!chooser.browseForFileToOpen())
        return;
    bool sentData = false;
    const auto result = chooser.getResult();
    m_previousPath = result.getParentDirectory().getFullPathName();
    const auto ext = result.getFileExtension().toLowerCase();
    if (ext == ".syx")
    {
        juce::MemoryBlock data;
        result.loadFileAsData(data);
        if (data.getSize() % 267 != 0)
        {
            return;
        }
        for (auto it = data.begin(); it != data.end(); it += 267)
        {
            if ((it + 267) < data.end())
            {
                m_controller.sendSysEx(Virus::SysEx(it, it + 267));
                sentData = true;
            }
        }
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
            if ((uint8_t)*it == (uint8_t)0xf0 && (it + 267) < end)
            {
                if ((uint8_t) * (it + 1) == (uint8_t)0x00)
                {
                    auto syx = Virus::SysEx(it, it + 267);
                    syx[7] = 0x01; // force to bank a
                    syx[266] = 0xf7;

                    m_controller.sendSysEx(syx);

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
                    m_controller.sendSysEx(syx);
                    it += 266;
                }

                sentData = true;
            }
        }
    }

    if (sentData)
        m_controller.onStateLoaded();
}
