#include "VirusEditor.h"
#include "BinaryData.h"

#include "Virus_ArpEditor.h"
#include "Virus_FxEditor.h"
#include "Virus_LfoEditor.h"
#include "Virus_OscEditor.h"
#include "../VirusParameterBinding.h"

using namespace juce;

constexpr auto kPanelWidth = 1377;
constexpr auto kPanelHeight = 800;

VirusEditor::VirusEditor(VirusParameterBinding& _parameterBinding) : m_parameterBinding(_parameterBinding)
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
	for (auto i = 0; i < 16; i++)
	{
		m_partSelect[i].setBounds(34, 161 + i*(36), 39, 36);
		m_partSelect[i].setButtonText(juce::String(i));
		m_partSelect[i].setRadioGroupId(kPartGroupId);
		m_partSelect[i].setClickingTogglesState(true);
		m_partSelect[i].onClick = [this, i]() {
			this->changePart(i);
		};
		addAndMakeVisible(m_partSelect[i]);
	}
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
	/* for (auto i = 0; i < 16; i++)
	{
		m_partSelect[i].setButtonText(juce::String(i));
		/m_partSelect[i].onClick = [this, i]() { changePart(i); };
		addAndMakeVisible(m_partSelect[i]);
	}*/
}