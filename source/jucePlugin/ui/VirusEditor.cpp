#include "VirusEditor.h"
#include "BinaryData.h"

#include "Virus_ArpEditor.h"
#include "Virus_FxEditor.h"
#include "Virus_LfoEditor.h"
#include "Virus_OscEditor.h"
#include "../VirusParameterBinding.h"
#include "../VirusController.h"

using namespace juce;

constexpr auto kPanelWidth = 1377;
constexpr auto kPanelHeight = 800;

VirusEditor::VirusEditor(VirusParameterBinding &_parameterBinding, Virus::Controller& _controller) :
    m_parameterBinding(_parameterBinding), m_controller(_controller)
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
        m_presetNames[pt].setButtonText(_controller.getCurrentPartPresetName(pt));
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
    }
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
void VirusEditor::changePart(uint8_t _part) {

    m_parameterBinding.m_part = _part;
    removeChildComponent(m_oscEditor.get());
    m_oscEditor	= std::make_unique<OscEditor>(m_parameterBinding);
    addChildComponent(m_oscEditor.get());

    removeChildComponent(m_lfoEditor.get());
    m_lfoEditor = std::make_unique<LfoEditor>(m_parameterBinding);
    addChildComponent(m_lfoEditor.get());

    removeChildComponent(m_fxEditor.get());
    m_fxEditor = std::make_unique<FxEditor>(m_parameterBinding);
    addChildComponent(m_fxEditor.get());

    removeChildComponent(m_arpEditor.get());
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
