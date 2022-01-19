#include "VirusEditor.h"
#include "BinaryData.h"
#include "../version.h"
#include "Virus_Panel4_ArpEditor.h"
#include "Virus_Panel3_FxEditor.h"
#include "Virus_Panel2_LfoEditor.h"
#include "Virus_Panel1_OscEditor.h"
#include "Virus_Panel5_PatchBrowser.h"
#include "../VirusParameterBinding.h"
#include "../VirusController.h"
#include "Ui_Utils.h"

using namespace juce;
 
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
    m_parameterBinding(_parameterBinding), processorRef(_processorRef), m_controller(_processorRef.getController()) ,m_controlLabel("ctrlLabel", ""), m_properties(_processorRef.getController().getConfig())
{
    setLookAndFeel(&m_lookAndFeel);

    m_background = Drawable::createFromImageData(BinaryData::main_background_png, BinaryData::main_background_pngSize);
    m_background->setBufferedToImage (true);

    addAndMakeVisible (*m_background);
    addAndMakeVisible (m_mainButtons);

    m_arpEditor = std::make_unique<ArpEditor>(_parameterBinding, processorRef);
    m_fxEditor = std::make_unique<FxEditor>(_parameterBinding);
    m_lfoEditor = std::make_unique<LfoEditor>(_parameterBinding);
    m_oscEditor = std::make_unique<OscEditor>(_parameterBinding);
    m_patchBrowser = std::make_unique<PatchBrowser>(_parameterBinding, processorRef);
    
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
    applyToSections([this, index](Component *s) mutable 
    { 
        if (currentTab == index) 
        {
            s->setVisible(true);
        }
        index++;
    });

    index = 0;
    m_mainButtons.applyToMainButtons([this, &index](DrawableButton *s) mutable 
    {
        if (currentTab == index) 
        {
            s->setToggleState(true, juce::dontSendNotification);
        }
        index++;
    });
    
    m_mainMenu.setLookAndFeel (&m_landfButtons);

    m_mainMenu.onClick = [this]()
    {
        selectorMenu.setLookAndFeel(&m_landfButtons);
        SubSkinSizeSelector.setLookAndFeel(&m_landfButtons);
        SubSkinSizeSelector.clear();

		for (float d = 375; d < 1250; d = d + 125)
		{
			SubSkinSizeSelector.addItem(std::to_string(200 * int(d) / 1000) + "%", [this, d] 
            {
				double dScaleFactor = float(d / 1000.0);               
	            m_AudioPlugInEditor->setScaleFactor(dScaleFactor);
	            (*this).setSize(iSkinSizeWidth, iSkinSizeHeight);                
                m_properties->setValue("skin_scale_factor", juce::String(dScaleFactor));
                m_properties->save();
            });
		}        
              
        selectorMenu.addSubMenu("Skin size", SubSkinSizeSelector, true);
		selectorMenu.addItem("About", [this]() { AboutWindow(); });
        selectorMenu.showMenu(juce::PopupMenu::Options());
	};

    //draw Main Menu Button	
    m_mainMenu.setBounds(2301 - m_mainMenu.kWidth / 2, 62 - m_mainMenu.kHeight / 2, m_mainMenu.kWidth, m_mainMenu.kHeight);  
    addAndMakeVisible(m_mainMenu);

    //MainDisplay (Patchname)
    m_patchName.setBounds(1473, 35, 480, 58);
    m_patchName.setJustificationType(Justification::left);
    m_patchName.setFont(juce::Font("Register", "Normal", 30.f));
    m_patchName.setEditable(false, true, true);
    m_patchName.onTextChange = [this]() 
    {
        auto text = m_patchName.getText();
        if(text.trim().length() > 0) {
            if (text == "/pv") { // stupid debug thing to remove later
                m_paramDisplayLocal = !m_paramDisplayLocal;
                return;
            }
            m_controller.setSinglePresetName(m_controller.getCurrentPart(), text);
        }
    };
    addAndMakeVisible(m_patchName);

    //MainDisplay 
    m_controlLabel.setBounds(1473, 35, 650, 58);
    m_controlLabel.setFont(juce::Font("Register", "Normal", 30.f));
    addAndMakeVisible(m_controlLabel);

    //ToolTip
    //m_ToolTip.setLookAndFeel(&m_landfToolTip);
    m_ToolTip.setBounds(200, 50, 200, 200);
    m_ToolTip.setFont(juce::Font("Register", "Normal", 15.f));
    m_ToolTip.setColour(juce::Label::ColourIds::backgroundColourId, juce::Colours::black);
    m_ToolTip.setColour(juce::Label::ColourIds::textColourId, juce::Colours::white);
    m_ToolTip.setJustificationType(Justification::centred);
    m_ToolTip.setAlpha(0.90);
    addAndMakeVisible(m_ToolTip);

    //MainDisplay Value
    m_controlLabelValue.setBounds(1900, 35, 197, 58);
    m_controlLabelValue.setJustificationType(Justification::centredRight);
    //m_controlLabelValue.setColour(juce::Label::textColourId, juce::Colours::red);
    m_controlLabelValue.setFont(juce::Font("Register", "Normal", 30.f));
    addAndMakeVisible(m_controlLabelValue);

     //m_controller.setCurrentPartPreset(0,virusLib::BankNumber::A,0);    

    //PresetsSwitch
    addAndMakeVisible(m_PresetLeft);
    addAndMakeVisible(m_PresetRight);
	m_PresetLeft.setBounds(2166 - m_PresetLeft.kWidth / 2, 62 - m_PresetLeft.kHeight / 2, m_PresetLeft.kWidth, m_PresetLeft.kHeight);  
	m_PresetRight.setBounds(2199 - m_PresetRight.kWidth / 2, 62 - m_PresetRight.kHeight / 2, m_PresetRight.kWidth, m_PresetRight.kHeight);  

	m_PresetLeft.onClick = [this]() {
		processorRef.getController().setCurrentPartPreset(m_controller.getCurrentPart(), processorRef.getController().getCurrentPartBank(m_controller.getCurrentPart()),
															std::max(0, processorRef.getController().getCurrentPartProgram(m_controller.getCurrentPart()) - 1));
	};
	m_PresetRight.onClick = [this]() 
    {
		processorRef.getController().setCurrentPartPreset(m_controller.getCurrentPart(), processorRef.getController().getCurrentPartBank(m_controller.getCurrentPart()),
															std::min(127, processorRef.getController().getCurrentPartProgram(m_controller.getCurrentPart()) + 1));
	};

    addAndMakeVisible(m_PresetPatchList);
	m_PresetPatchList.setBounds(2133 - m_PresetPatchList.kWidth / 2, 62 - m_PresetPatchList.kHeight / 2, m_PresetPatchList.kWidth, m_PresetPatchList.kHeight);  
    m_PresetPatchList.onClick = [this]() {ShowMenuePatchList(); };

    //Show Version
    m_version.setText(std::string(g_pluginVersionString), NotificationType::dontSendNotification);
    m_version.setBounds(250, 1124, 50, 17);
    m_version.setColour(juce::Label::textColourId, juce::Colours::silver);
    m_version.setFont(juce::Font("Arial", "Bold", 20.f));
    m_version.setJustificationType(Justification::left);
    m_version.setJustificationType(Justification::centred);
    addAndMakeVisible(m_version);
    
    //Show Synth Model
    m_SynthModel.setText(m_controller.getVirusModel() == virusLib::VirusModel::B ? "B" : "C", NotificationType::dontSendNotification);
    m_SynthModel.setBounds(430, 1124, 50, 17);
    m_SynthModel.setFont(juce::Font("Arial", "Bold", 20.f));
    m_SynthModel.setJustificationType(Justification::left);
    m_SynthModel.setColour(juce::Label::textColourId, juce::Colours::silver);
    m_SynthModel.setJustificationType(Justification::centred);
    addAndMakeVisible(m_SynthModel);

    //Show RomName
    m_RomName.setText(_processorRef.getRomName()+".bin", NotificationType::dontSendNotification);
    m_RomName.setBounds(642, 1124, 150, 17);
    m_RomName.setColour(juce::Label::textColourId, juce::Colours::silver);
    m_RomName.setFont(juce::Font("Arial", "Bold", 20.f));
    m_RomName.setJustificationType(Justification::left);
    m_RomName.setJustificationType(Justification::centred);
    addAndMakeVisible(m_RomName);

    m_controller.onProgramChange = [this]() 
    {
        updateParts();
    };

    m_controller.getBankCount();
    addMouseListener(this, true);
    setSize (kPanelWidth, kPanelHeight);
    recreateControls();
	updateParts();
}

VirusEditor::~VirusEditor()
{
    m_mainMenu.onClick = nullptr;	
    selectorMenu.setLookAndFeel(nullptr);
	SubSkinSizeSelector.setLookAndFeel(nullptr);
	m_mainMenu.setLookAndFeel (nullptr);
    selector.setLookAndFeel (nullptr); 
	m_controller.onProgramChange = nullptr; 
    setLookAndFeel(nullptr);
}

void VirusEditor::updateParts() 
{
    const auto multiMode = m_controller.isMultiMode();
    for (auto pt = 0; pt < 16; pt++)
    {
        bool singlePartOrInMulti = pt == 0 || multiMode;
        if (pt == m_controller.getCurrentPart())
        {
            int iZeroCount;
            if (m_controller.getCurrentPartProgram(m_controller.getCurrentPart()) < 10) iZeroCount = 2;
            else if (m_controller.getCurrentPartProgram(m_controller.getCurrentPart()) < 100) iZeroCount = 1;
            else iZeroCount = 0;

            const auto patchName = m_controller.getCurrentPartPresetName(pt);
            if(m_patchName.getText() != patchName) 
            {
                String  sZero = "";
                for (int i=0; i<iZeroCount; i++)
                {
                    sZero=sZero+"0";
                }
                m_patchName.setText("["+juce::String(m_controller.getCurrentPart()+1)
                    +"][" + getCurrentPartBankStr(m_controller.getCurrentPartBank(m_controller.getCurrentPart())) + "] "                    
                    + sZero
                    + juce::String(processorRef.getController().getCurrentPartProgram(m_controller.getCurrentPart())+1)+": " + patchName, NotificationType::dontSendNotification);
            } 
        }
    } 
}


void VirusEditor::ShowMenuePatchList()
{
    auto pt = m_controller.getCurrentPart();
    
    selector.setLookAndFeel (&m_landfButtons);    
    
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
                //m_presetNames[pt].setButtonText(presetName);
            });
        }
        std::stringstream bankName;
        bankName << "Bank " << static_cast<char>('A' + b);
        selector.addSubMenu(std::string(bankName.str()), p);
    }
    selector.showMenu(juce::PopupMenu::Options());
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
    for (auto *section : {static_cast<juce::DrawableButton *>(&m_arpSettings), static_cast<DrawableButton *>(&m_effects),
                          static_cast<DrawableButton *>(&m_lfoMatrix), static_cast<DrawableButton *>(&m_oscFilter),
                          static_cast<DrawableButton *>(&m_patches)})
    {
        action(section);
    }
}


void VirusEditor::updateControlLabel(Component* eventComponent) 
{
    auto props = eventComponent->getProperties();
    if(props.contains("type") && props["type"] == "slider") {
        
        auto comp = dynamic_cast<juce::Slider*>(eventComponent);
        if(comp && comp->isEnabled()) 
        {
            int iValue = (props.contains("bipolar") && props["bipolar"])?juce::roundToInt(comp->getValue())-64:juce::roundToInt(comp->getValue());
            
            //ToolTip handle
            m_ToolTip.setVisible(true);
            m_ToolTip.setText(std::to_string(iValue), juce::dontSendNotification);
            //todo --> ugly --> Need to be fixed
            m_ToolTip.setBounds(comp->getX()+comp->getWidth()*2-43, comp->getY()+comp->getHeight()*2+83, 70,30);
            m_ToolTip.toFront(true);

            //Main display handle
            m_controlLabel.setVisible(true);
            m_controlLabelValue.setVisible(true);
            m_patchName.setVisible(false);

            if(m_paramDisplayLocal) 
            {
                m_controlLabel.setColour(juce::Label::ColourIds::backgroundColourId, juce::Colours::black);
                m_controlLabel.setColour(juce::Label::ColourIds::outlineColourId, juce::Colours::white); 
            }

            m_controlLabel.setText(props["name"].toString(), juce::dontSendNotification);
            m_controlLabelValue.setText(juce::String(iValue), juce::dontSendNotification);
        }
    }

}
void VirusEditor::mouseDrag(const juce::MouseEvent & event)
{
    updateControlLabel(event.eventComponent);
}

void VirusEditor::mouseEnter(const juce::MouseEvent& event) {
    if (event.mouseWasDraggedSinceMouseDown()) {
        return;
    }
    updateControlLabel(event.eventComponent);
}

void VirusEditor::mouseExit(const juce::MouseEvent& event) {
    if (event.mouseWasDraggedSinceMouseDown()) {
        return;
    }
    m_controlLabel.setText("", juce::dontSendNotification);
    m_controlLabel.setVisible(false); 
    m_controlLabelValue.setVisible(false);
    m_patchName.setVisible(true);
    m_ToolTip.setVisible(false);
}

void VirusEditor::mouseDown(const juce::MouseEvent &event) {
    
}

void VirusEditor::mouseUp(const juce::MouseEvent & event)
{
    m_controlLabel.setText("", juce::dontSendNotification);
    m_controlLabel.setVisible(false); 
    m_controlLabelValue.setVisible(false);
    m_patchName.setVisible(true);
    m_ToolTip.setVisible(false);
}
void VirusEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    updateControlLabel(event.eventComponent);
}


void VirusEditor::resized()
{
    m_background->setBounds (getLocalBounds());

	//Position of first main button (osc/filt)
    m_mainButtons.setBounds(121, 36, m_mainButtons.getWidth(), m_mainButtons.getHeight());

    // Panel Positions
    applyToSections([this](Component *s) { s->setTopLeftPosition(101, 120); });
}

void VirusEditor::handleCommandMessage(int commandId) 
{
    switch (commandId) {
        case Commands::Rebind: recreateControls();
        case Commands::UpdateParts: { updateParts();};
        default: return;
    }
}

void VirusEditor::recreateControls()
{
    removeChildComponent(m_oscEditor.get());
    removeChildComponent(m_lfoEditor.get());
    removeChildComponent(m_fxEditor.get());
    removeChildComponent(m_arpEditor.get());
    removeChildComponent(m_patchBrowser.get());

    m_oscEditor	= std::make_unique<OscEditor>(m_parameterBinding);
    addChildComponent(m_oscEditor.get());

    m_lfoEditor = std::make_unique<LfoEditor>(m_parameterBinding);
    addChildComponent(m_lfoEditor.get());

    m_fxEditor = std::make_unique<FxEditor>(m_parameterBinding);
    addChildComponent(m_fxEditor.get());

    m_arpEditor = std::make_unique<ArpEditor>(m_parameterBinding, processorRef);
    addChildComponent(m_arpEditor.get());

    m_patchBrowser = std::make_unique<PatchBrowser>(m_parameterBinding, processorRef);
    addChildComponent(m_patchBrowser.get());

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
	setupButton(0, Drawable::createFromImageData(BinaryData::btn_main_1_png, BinaryData::btn_main_1_pngSize), m_oscFilter);
	setupButton(1, Drawable::createFromImageData(BinaryData::btn_main_2_png, BinaryData::btn_main_2_pngSize), m_lfoMatrix);
	setupButton(2, Drawable::createFromImageData(BinaryData::btn_main_3_png, BinaryData::btn_main_3_pngSize), m_effects);
	setupButton(3, Drawable::createFromImageData(BinaryData::btn_main_4_png, BinaryData::btn_main_4_pngSize), m_arpSettings);
	setupButton(4, Drawable::createFromImageData(BinaryData::btn_main_5_png, BinaryData::btn_main_5_pngSize), m_patches);
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
    btn.setBounds ((i > 1 ? -1 : 0) + i * (kButtonWidth + kMargin)-5, 0, kButtonWidth, kButtonHeight);
    btn.getToggleStateValue().addListener(this);
    addAndMakeVisible (btn);
}


void VirusEditor::AboutWindow() {}

