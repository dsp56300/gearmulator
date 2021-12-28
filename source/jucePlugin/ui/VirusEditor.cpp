#include "VirusEditor.h"
#include "BinaryData.h"

#include "Virus_ArpEditor.h"
#include "Virus_FxEditor.h"
#include "Virus_LfoEditor.h"
#include "Virus_OscEditor.h"

using namespace juce;

constexpr auto kPanelWidth = 1377;
constexpr auto kPanelHeight = 800;

VirusEditor::VirusEditor()
{
    setLookAndFeel(&m_lookAndFeel);

    m_background = Drawable::createFromImageData (BinaryData::bg_1377x800_png, BinaryData::bg_1377x800_pngSize);

    m_background->setBufferedToImage (true);
    addAndMakeVisible (*m_background);
    addAndMakeVisible (m_mainButtons);

    m_arpEditor = std::make_unique<ArpEditor>();
    m_fxEditor = std::make_unique<FxEditor>();
    m_lfoEditor = std::make_unique<LfoEditor>();
    m_oscEditor = std::make_unique<OscEditor>();

    applyToSections([this](Component *s) { addChildComponent(s); });

    // show/hide section from buttons..
    m_mainButtons.updateSection = [this]() {
        m_arpEditor->setVisible(m_mainButtons.m_arpSettings.getToggleState());
        m_fxEditor->setVisible(m_mainButtons.m_effects.getToggleState());
        m_lfoEditor->setVisible(m_mainButtons.m_lfoMatrix.getToggleState());
        m_oscEditor->setVisible(m_mainButtons.m_oscFilter.getToggleState());
    };

    addAndMakeVisible(m_presetButtons);

    setSize (kPanelWidth, kPanelHeight);
}

VirusEditor::~VirusEditor() { setLookAndFeel(nullptr); }

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
