#include "VirusEditor.h"
#include "BinaryData.h"
#include "../version.h"
#include "Virus_ArpEditor.h"
#include "Virus_FxEditor.h"
#include "Virus_LfoEditor.h"
#include "Virus_OscEditor.h"
#include "Virus_PatchBrowser.h"
#include "Virus_Parts.h"
#include "../VirusParameterBinding.h"
#include "../VirusController.h"

using namespace juce;

constexpr auto kPanelWidth = 1377;
constexpr auto kPanelHeight = 800;
enum Tabs
{
	ArpSettings,
	Effects,
	LfoMatrix,
	OscFilter,
	Patches
};
static uint8_t currentTab = Tabs::OscFilter;

VirusEditor::VirusEditor(VirusParameterBinding &_parameterBinding, AudioPluginAudioProcessor &_processorRef) :
    m_parameterBinding(_parameterBinding), processorRef(_processorRef), m_controller(processorRef.getController()),
    m_controlLabel("ctrlLabel", "")
{
    setLookAndFeel(&m_lookAndFeel);

    m_background = Drawable::createFromImageData (BinaryData::bg_1377x800_png, BinaryData::bg_1377x800_pngSize);

    m_background->setBufferedToImage (true);

    addAndMakeVisible (*m_background);
    addAndMakeVisible (m_mainButtons);

    m_arpEditor = std::make_unique<ArpEditor>(_parameterBinding);
    m_fxEditor = std::make_unique<FxEditor>(_parameterBinding, m_controller);
    m_lfoEditor = std::make_unique<LfoEditor>(_parameterBinding);
    m_oscEditor = std::make_unique<OscEditor>(_parameterBinding);
    m_patchBrowser = std::make_unique<PatchBrowser>(_parameterBinding, m_controller);
    m_partList = std::make_unique<Parts>(_parameterBinding, m_controller);

    m_partList->setBounds(0,0, 338, kPanelHeight);
    m_partList->setVisible(true);
    addChildComponent(m_partList.get());

    applyToSections([this](Component *s) { addChildComponent(s); });

    // show/hide section from buttons..
    m_mainButtons.updateSection = [this]() {
        if (m_mainButtons.m_arpSettings.getToggleState()) {
            currentTab = Tabs::ArpSettings;
        }
        else if (m_mainButtons.m_effects.getToggleState()) {
            currentTab = Tabs::Effects;
        }
        else if (m_mainButtons.m_lfoMatrix.getToggleState()) {
            currentTab = Tabs::LfoMatrix;
        }
        else if (m_mainButtons.m_oscFilter.getToggleState()) {
            currentTab = Tabs::OscFilter;
        }
        else if (m_mainButtons.m_patches.getToggleState()) {
            currentTab = Tabs::Patches;
        }
        m_arpEditor->setVisible(m_mainButtons.m_arpSettings.getToggleState());
        m_fxEditor->setVisible(m_mainButtons.m_effects.getToggleState());
        m_lfoEditor->setVisible(m_mainButtons.m_lfoMatrix.getToggleState());
        m_oscEditor->setVisible(m_mainButtons.m_oscFilter.getToggleState());
        m_patchBrowser->setVisible(m_mainButtons.m_patches.getToggleState());
    };
    uint8_t index = 0;
    applyToSections([this, index](Component *s) mutable { 
        if (currentTab == index) {
            s->setVisible(true);
        }
        index++;
    });
    index = 0;
    m_mainButtons.applyToMainButtons([this, &index](DrawableButton *s) mutable {
        if (currentTab == index) {
            s->setToggleState(true, dontSendNotification);
        }
        index++;
    });
    //m_oscEditor->setVisible(true);
    //m_mainButtons.m_oscFilter.setToggleState(true, NotificationType::dontSendNotification);
    addAndMakeVisible(m_presetButtons);
    m_presetButtons.m_load.onClick = [this]() { loadFile(); };
    m_presetButtons.m_save.onClick = [this]() { saveFile(); };

    m_properties = m_controller.getConfig();
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
    auto midiInputs = MidiInput::getAvailableDevices();
    StringArray midiInputNames;
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
    m_cmbMidiInput.setSelectedItemIndex(inIndex, dontSendNotification);
    m_cmbMidiOutput.setTextWhenNoChoicesAvailable("No MIDI Outputs Enabled");
    auto midiOutputs = MidiOutput::getAvailableDevices();
    StringArray midiOutputNames;
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
    m_cmbMidiOutput.setSelectedItemIndex(outIndex, dontSendNotification);
    m_cmbMidiInput.onChange = [this]() { updateMidiInput(m_cmbMidiInput.getSelectedItemIndex()); };
    m_cmbMidiOutput.onChange = [this]() { updateMidiOutput(m_cmbMidiOutput.getSelectedItemIndex()); };

    std::string message =
        "DSP 56300 Emulator\nVersion " + std::string(g_pluginVersionString) + "\n" __DATE__ " " __TIME__;
    if (!processorRef.isPluginValid())
        message += "\n\nNo ROM, no sound!\nCopy ROM next to plugin, must end with .bin";
    message += "\n\nTo donate: paypal.me/dsp56300";
    std::string model(m_controller.getVirusModel() == virusLib::PresetVersion::B ? "B" : "C");
    message += "\n\nROM Loaded: " + _processorRef.getRomName();
    m_version.setText(message, NotificationType::dontSendNotification);
    m_version.setBounds(94, 2, 220, 150);
    m_version.setColour(Label::textColourId, Colours::white);
    m_version.setJustificationType(Justification::centred);
    addAndMakeVisible(m_version);

    m_patchName.setBounds(410, 48, 362, 36);
    m_patchName.setJustificationType(Justification::left);
    m_patchName.setFont(Font(32.0f, Font::bold));
    m_patchName.setColour(Label::textColourId, Colour(255, 113, 128));
    m_patchName.setEditable(false, true, true);
    m_patchName.onTextChange = [this]() {
        auto text = m_patchName.getText();
        if(text.trim().length() > 0) {
            m_controller.setSinglePresetName(m_controller.getCurrentPart(), text);
            m_partList->refreshParts();
        }
    };
    addAndMakeVisible(m_patchName);

    m_controlLabel.setBounds(m_patchName.getBounds().translated(m_controlLabel.getWidth(), 0));
    m_controlLabel.setSize(m_patchName.getWidth()/2, m_patchName.getHeight());
    m_controlLabel.setJustificationType(Justification::topRight);
    m_controlLabel.setColour(Label::textColourId, Colour(255, 113, 128));
    m_controlLabel.setFont(Font(16.0f, Font::bold));
    //m_controlLabel.setColour(Label::ColourIds::backgroundColourId, Colours::black);
    //m_controlLabel.setColour(Label::ColourIds::outlineColourId, Colours::white);

    addAndMakeVisible(m_controlLabel);

    m_controller.onProgramChange = [this]() {
        updateParts();
        m_partList->refreshParts();
    };
    m_controller.getBankCount();
    addMouseListener(this, true);

    setSize (kPanelWidth, kPanelHeight);

    recreateControls();
	updateParts();
}

VirusEditor::~VirusEditor()
{
	setLookAndFeel(nullptr);
	m_controller.onProgramChange = nullptr;
}

void VirusEditor::updateParts() {
    const auto multiMode = m_controller.isMultiMode();
    for (auto pt = 0; pt < 16; pt++)
    {
        bool singlePartOrInMulti = pt == 0 || multiMode;
        if (pt == m_controller.getCurrentPart())
        {
            const auto patchName = m_controller.getCurrentPartPresetName(pt);
            if(m_patchName.getText() != patchName) {
                m_patchName.setText(patchName, NotificationType::dontSendNotification);
            }
        }
    }
}

void VirusEditor::updateMidiInput(int index)
{
    auto list = MidiInput::getAvailableDevices();

    if (index == 0)
    {
        m_properties->setValue("midi_input", "");
        m_properties->save();
        m_lastInputIndex = index;
        m_cmbMidiInput.setSelectedItemIndex(index, dontSendNotification);
        return;
    }
    index--;
    auto newInput = list[index];

    if (!deviceManager.isMidiInputDeviceEnabled(newInput.identifier))
        deviceManager.setMidiInputDeviceEnabled(newInput.identifier, true);

    if (!processorRef.setMidiInput(newInput.identifier))
    {
        m_cmbMidiInput.setSelectedItemIndex(0, dontSendNotification);
        m_lastInputIndex = 0;
        return;
    }

    m_properties->setValue("midi_input", newInput.identifier);
    m_properties->save();

    m_cmbMidiInput.setSelectedItemIndex(index + 1, dontSendNotification);
    m_lastInputIndex = index;
}
void VirusEditor::updateMidiOutput(int index)
{
    auto list = MidiOutput::getAvailableDevices();

    if (index == 0)
    {
        m_properties->setValue("midi_output", "");
        m_properties->save();
        m_cmbMidiOutput.setSelectedItemIndex(index, dontSendNotification);
        m_lastOutputIndex = index;
        processorRef.setMidiOutput("");
        return;
    }
    index--;
    auto newOutput = list[index];
    if (!processorRef.setMidiOutput(newOutput.identifier))
    {
        m_cmbMidiOutput.setSelectedItemIndex(0, dontSendNotification);
        m_lastOutputIndex = 0;
        return;
    }
    m_properties->setValue("midi_output", newOutput.identifier);
    m_properties->save();

    m_cmbMidiOutput.setSelectedItemIndex(index + 1, dontSendNotification);
    m_lastOutputIndex = index;
}
void VirusEditor::applyToSections(std::function<void(Component *)> action)
{
    for (auto *section : {static_cast<Component *>(m_arpEditor.get()), static_cast<Component *>(m_fxEditor.get()),
                          static_cast<Component *>(m_lfoEditor.get()), static_cast<Component *>(m_oscEditor.get()),
                          static_cast<Component *>(m_patchBrowser.get())})
    {
        action(section);
    }
}
void VirusEditor::MainButtons::applyToMainButtons(std::function<void(DrawableButton *)> action)
{
    for (auto *section : {static_cast<DrawableButton *>(&m_arpSettings), static_cast<DrawableButton *>(&m_effects),
                          static_cast<DrawableButton *>(&m_lfoMatrix), static_cast<DrawableButton *>(&m_oscFilter),
                          static_cast<DrawableButton *>(&m_patches)})
    {
        action(section);
    }
}

void VirusEditor::updateControlLabel(Component* eventComponent) {
    auto props = eventComponent->getProperties();
    if(props.contains("type") && props["type"] == "slider") {
        m_controlLabel.setVisible(true);
        auto comp = dynamic_cast<Slider*>(eventComponent);
        if(comp) {
            auto name = props["name"];
            if(m_paramDisplayLocal) {
                m_controlLabel.setTopLeftPosition(getTopLevelComponent()->getLocalPoint(comp->getParentComponent(), comp->getPosition().translated(0, -16)));
                m_controlLabel.setSize(comp->getWidth(), 20);
                m_controlLabel.setColour(Label::ColourIds::backgroundColourId, Colours::black);
                m_controlLabel.setColour(Label::ColourIds::outlineColourId, Colours::white);
                if (props.contains("bipolar") && props["bipolar"]) {
                    m_controlLabel.setText(String(roundToInt(comp->getValue())-64), dontSendNotification);
                } else {
                    m_controlLabel.setText(String(roundToInt(comp->getValue())), dontSendNotification);
                }
            }
            else {
                m_controlLabel.setBounds(m_patchName.getBounds().translated(m_controlLabel.getWidth(), 0));
                m_controlLabel.setSize(m_patchName.getWidth()/2, m_patchName.getHeight());
                if (props.contains("bipolar") && props["bipolar"]) {
                    m_controlLabel.setText(name.toString() + "\n" + String(roundToInt(comp->getValue())-64), dontSendNotification);
                } else {
                    m_controlLabel.setText(name.toString() + "\n" + String(roundToInt(comp->getValue())), dontSendNotification);
                }
            }
        }
    }
}
void VirusEditor::mouseDrag(const MouseEvent & event)
{
    updateControlLabel(event.eventComponent);
}

void VirusEditor::mouseEnter(const MouseEvent& event) {
    if (event.mouseWasDraggedSinceMouseDown()) {
        return;
    }
    updateControlLabel(event.eventComponent);
}
void VirusEditor::mouseExit(const MouseEvent& event) {
    if (event.mouseWasDraggedSinceMouseDown()) {
        return;
    }
    m_controlLabel.setText("", dontSendNotification);
}
void VirusEditor::mouseDown(const MouseEvent &event) {
    
}
void VirusEditor::mouseUp(const MouseEvent & event)
{
    m_controlLabel.setText("", dontSendNotification);
    m_controlLabel.setVisible(false);
}
void VirusEditor::mouseWheelMove(const MouseEvent& event, const MouseWheelDetails& wheel) {
    updateControlLabel(event.eventComponent);
}
void VirusEditor::resized()
{
    m_background->setBounds (getLocalBounds());
    m_mainButtons.setBounds (394, 106, m_mainButtons.getWidth(), m_mainButtons.getHeight());
    auto statusArea = Rectangle<int>(395, 36, 578, 60);
    m_presetButtons.setBounds(statusArea.removeFromRight(188));
    applyToSections([this](Component *s) { s->setTopLeftPosition(338, 133); });
}

void VirusEditor::handleCommandMessage(int commandId) {
    switch (commandId) {
        case Commands::Rebind: recreateControls();
        case Commands::UpdateParts: { updateParts(); m_partList->refreshParts(); };
        default: return;
    }
}

void VirusEditor::recreateControls()
{
    removeChildComponent(m_oscEditor.get());
    removeChildComponent(m_lfoEditor.get());
    removeChildComponent(m_fxEditor.get());
    removeChildComponent(m_arpEditor.get());

    m_oscEditor	= std::make_unique<OscEditor>(m_parameterBinding);
    addChildComponent(m_oscEditor.get());

    m_lfoEditor = std::make_unique<LfoEditor>(m_parameterBinding);
    addChildComponent(m_lfoEditor.get());

    m_fxEditor = std::make_unique<FxEditor>(m_parameterBinding, m_controller);
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
, m_patches("PATCHES", DrawableButton::ImageRaw)
{
    constexpr auto numOfMainButtons = 5;
    setupButton (0, Drawable::createFromImageData (BinaryData::GLOBAL_btn_osc_filter_141x26_png, BinaryData::GLOBAL_btn_osc_filter_141x26_pngSize), m_oscFilter);
    setupButton (1, Drawable::createFromImageData (BinaryData::GLOBAL_btn_lfo_matrix_141x26_png, BinaryData::GLOBAL_btn_lfo_matrix_141x26_pngSize), m_lfoMatrix);
    setupButton (2, Drawable::createFromImageData (BinaryData::GLOBAL_btn_effects_141x26_png, BinaryData::GLOBAL_btn_effects_141x26_pngSize), m_effects);
    setupButton (3, Drawable::createFromImageData (BinaryData::GLOBAL_btn_arp_settings_141x26_png, BinaryData::GLOBAL_btn_arp_settings_141x26_pngSize), m_arpSettings);
    setupButton (4,  Drawable::createFromImageData(BinaryData::GLOBAL_btn_patch_browser_141x26_png, BinaryData::GLOBAL_btn_patch_browser_141x26_pngSize), m_patches);
    setSize ((kButtonWidth + kMargin) * numOfMainButtons, kButtonHeight);
}


void VirusEditor::MainButtons::valueChanged(Value &) { updateSection(); }

void VirusEditor::MainButtons::setupButton (int i, std::unique_ptr<Drawable>&& btnImage, DrawableButton& btn)
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
    m_fileChooser = std::make_unique<FileChooser>(
        "Choose syx/midi banks to import",
        m_previousPath.isEmpty()
            ? File::getSpecialLocation(File::currentApplicationFile).getParentDirectory()
            : m_previousPath,
        "*.syx,*.mid,*.midi", true);
    
    const auto flags = FileBrowserComponent::openMode | FileBrowserComponent::FileChooserFlags::canSelectFiles;

    const std::function onFileChooser = [this](const FileChooser& chooser)
    {
        if (chooser.getResults().isEmpty())
            return;

        const auto result = chooser.getResult();
        m_previousPath = result.getParentDirectory().getFullPathName();
        const auto ext = result.getFileExtension().toLowerCase();

        std::vector<Patch> patches;
		PatchBrowser::loadBankFile(patches, nullptr, result);

		if(patches.empty())
			return;

    	if(patches.size() == 1)
        {
	        // load to edit buffer of current part
	        auto data = patches.front().sysex;
	        data[7] = virusLib::toMidiByte(virusLib::BankNumber::EditBuffer);
	        if (m_controller.isMultiMode())
		        data[8] = m_controller.getCurrentPart();
	        else
		        data[8] = virusLib::SINGLE;
	        m_controller.sendSysEx(data);
        }
        else
        {
	        // load to bank A
	        for (const auto& p : patches)
	        {
		        auto data = p.sysex;
		        data[7] = virusLib::toMidiByte(virusLib::BankNumber::A);
		        m_controller.sendSysEx(data);
	        }
        }

        m_controller.onStateLoaded();
    };
    m_fileChooser->launchAsync (flags, onFileChooser);
}

void VirusEditor::saveFile() {
    auto path = m_controller.getConfig()->getValue("virus_bank_dir", "");
    m_fileChooser = std::make_unique<FileChooser>(
        "Save preset as syx",
        m_previousPath.isEmpty()
            ? (path.isEmpty() ? File::getSpecialLocation(File::currentApplicationFile).getParentDirectory() : File(path))
            : m_previousPath,
        "*.syx", true);
    const auto flags = FileBrowserComponent::saveMode | FileBrowserComponent::FileChooserFlags::canSelectFiles;


    std::function<void (const FileChooser &)> onFileChooser = [this](const FileChooser& chooser)
    {
        if (chooser.getResults().isEmpty())
            return;
        bool sentData = false;
        const auto result = chooser.getResult();
        m_previousPath = result.getParentDirectory().getFullPathName();
        const auto ext = result.getFileExtension().toLowerCase();
        const uint8_t syxHeader[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x01, 0x00};
        const uint8_t syxEof[1] = {0xF7};
        uint8_t cs = syxHeader[5] + syxHeader[6] + syxHeader[7] + syxHeader[8];
        uint8_t data[256];
        for (int i = 0; i < 256; i++)
        {
            auto param = m_controller.getParamValue(m_controller.getCurrentPart(), i < 128 ? 0 : 1, i % 128);

            data[i] = param ? (int)param->getValue() : 0;
            cs += data[i];
        }
        cs = cs & 0x7f;

        result.deleteFile();
        result.create();
        result.appendData(syxHeader, 9);
        result.appendData(data, 256);
        result.appendData(&cs, 1);
        result.appendData(syxEof, 1);
    };
    m_fileChooser->launchAsync (flags, onFileChooser);
}
