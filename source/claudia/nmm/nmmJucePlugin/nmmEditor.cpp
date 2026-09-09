#include "nmmEditor.h"
#include "nmmPluginProcessor.h"
#include "jucePluginLib/controller.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "jucePluginEditorLib/settingsDeviceSpecific.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Input.h"
#include "skins.h"

namespace nmmJucePlugin
{
    std::unique_ptr<jucePluginEditorLib::SettingsDeviceSpecific> Editor::createDeviceSpecificSettings(const std::string& name,Rml::Element* root)
    {
        if(name!="tus_settings_midi_NordMicroModular") return nullptr;
        auto* toggle=juceRmlUi::helper::findChild(root,"EditorMidiEnabled");
        auto* button=juceRmlUi::helper::findChild(toggle,"button");
        auto* ports=juceRmlUi::helper::findChild(root,"EditorMidiPorts");
        auto update=[this,button,ports]
        {
            juceRmlUi::ElemButton::setChecked(button,m_processor.editorMidiEnabled());
            std::lock_guard<std::mutex> lock(m_panel->editor->mutex);
            ports->SetInnerRML(Rml::StringUtilities::EncodeRml(m_panel->editor->ports));
        };
        update();
        juceRmlUi::EventListener::AddClick(toggle,[this,update]{m_processor.setEditorMidiEnabled(!m_processor.editorMidiEnabled());update();});
        return std::make_unique<jucePluginEditorLib::SettingsDeviceSpecific>();
    }
    EditorState::EditorState(Processor& p):PluginEditorState(p,p.getController(),g_includedSkins) {loadDefaultSkin();}
    jucePluginEditorLib::Editor* EditorState::createEditor(const jucePluginEditorLib::Skin& skin)
    {return new Editor(static_cast<Processor&>(m_processor),skin);}
    void EditorState::initContextMenu(juceRmlUi::Menu& menu)
    {
        if(auto* editor=dynamic_cast<Editor*>(getEditor()))
            menu.addEntry("Load Patch...", [editor]{editor->loadPatch();});
    }
    Editor::Editor(Processor& p,const jucePluginEditorLib::Skin& skin):jucePluginEditorLib::Editor(p,skin),m_processor(p),m_panel(p.panel()) {}
    void Editor::addStandaloneOptions(juce::PopupMenu& menu)
    {
        menu.addItem("Load Patch...", [this]{loadPatch();});
    }
    Editor::~Editor()
    {
        stopTimer();note(false);button4(false);setShift(false);m_chooser.reset();
        if(auto* component=getRmlComponent()) component->removeComponentListener(this);
    }
    void Editor::create()
    {
        jucePluginEditorLib::Editor::create(); // installs shared Escape settings, scaling, MIDI learn, etc.
        getRmlComponent()->addComponentListener(this);
        juceRmlUi::EventListener::Add(findChild("NoteTrig"),Rml::EventId::Mousedown,[this](Rml::Event& event)
        {
            if(event.GetParameter<int>("button",-1)!=0) return;
            if(m_shiftDown) note(true);
            else selectPatch(1);
        });
        juceRmlUi::EventListener::Add(findChild("Patch"),Rml::EventId::Mousedown,[this](Rml::Event& event)
        {
            if(event.GetParameter<int>("button",-1)!=0) return;
            if(m_shiftDown) button4(true);
            else selectPatch(-1);
        });
        juceRmlUi::EventListener::Add(getDocument(),Rml::EventId::Keydown,[this](Rml::Event& event)
        {
            if(event.GetParameter<int>("key_identifier",0)==Rml::Input::KI_ESCAPE) {note(false);button4(false);setShift(false);}
        },true);
        findChild("Shift")->SetAttribute("tooltip","Toggle alternate controls");
        juceRmlUi::EventListener::Add(findChild("Shift"),Rml::EventId::Click,[this](Rml::Event&)
        {
            setShift(!m_shiftDown);
        });
        juceRmlUi::EventListener::Add(getDocument(),Rml::EventId::Mouseup,[this](Rml::Event&){note(false);button4(false);},true);
        juceRmlUi::EventListener::Add(findChild("PatchSelect"),Rml::EventId::Change,[this](Rml::Event& event)
        {
            if(m_updatingPatchSelect) return;
            const auto index=static_cast<unsigned>(std::max(1,juce::roundToInt(event.GetParameter<float>("value",1)))-1);
            note(false);
            if(m_panel->selectPatch(index)) timerCallback();
        });
        startTimerHz(20);timerCallback();
    }
    void Editor::updatePatchSelectMode()
    {
        const bool shifted=m_shiftDown;
        findChild("Knob3")->SetProperty(Rml::PropertyId::Display,shifted?Rml::Style::Display::None:Rml::Style::Display::Block);
        findChild("PatchSelect")->SetProperty(Rml::PropertyId::Display,shifted?Rml::Style::Display::Block:Rml::Style::Display::None);
    }
    void Editor::setShift(bool down)
    {
        if(down==m_shiftDown) return;
        m_shiftDown=down;
        juceRmlUi::ElemButton::setChecked(findChild("Shift"),down);
        updatePatchSelectMode();
    }
    void Editor::selectPatch(int direction)
    {
        note(false);button4(false);
        if(m_panel->selectPatchRelative(direction)) timerCallback();
    }
    void Editor::button4(bool down)
    {
        if(down==m_button4Down) return;
        m_button4Down=down;
        m_panel->button4=down?127:0;
    }
    void Editor::note(bool down)
    {
        if(down==m_noteDown || (down && !m_panel->ready)) return;
        m_noteDown=down;
        m_processor.addMidiEvent(synthLib::SMidiEvent(synthLib::MidiEventSource::Editor,down?0x90:0x80,60,down?100:0));
    }
    void Editor::componentParentHierarchyChanged(juce::Component& component)
    {
        // The framework retains the skin editor when the host closes its window.
        if(!component.isShowing()) {note(false);button4(false);setShift(false);}
    }
    void Editor::componentVisibilityChanged(juce::Component& component)
    {
        if(!component.isShowing()) {note(false);button4(false);setShift(false);}
    }
    void Editor::loadPatch()
    {
        note(false);button4(false);setShift(false);
        m_chooser=std::make_unique<juce::FileChooser>("Load Nord Modular patch",juce::File(),"*.pch");
        const auto panel=m_panel;
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[panel](const juce::FileChooser& chooser)
        {
            const auto file=chooser.getResult();
            if(file.existsAsFile()) panel->requestPatch(file.loadFileAsString().toStdString(),file.getFileNameWithoutExtension().toStdString());
        });
    }
    void Editor::timerCallback()
    {
        bool changed=false;
        for(unsigned i=0;i<4;++i) {const auto value=m_panel->values[i].load();if(value!=m_lastValues[i]) {m_lastValues[i]=value;changed=true;}}
        if(changed) m_processor.getController().onStateLoaded();
        {std::lock_guard<std::mutex> lock(m_panel->editor->mutex);
            findChild("PanelStatus")->SetAttribute("tooltip",m_panel->editor->ports+" · Browser MIDI input: PC Out; output: PC In");}
        std::string status;unsigned selected=0,count=1;
        {std::lock_guard<std::mutex> lock(m_panel->mutex);status=m_panel->status;selected=m_panel->selectedPatch;count=static_cast<unsigned>(std::max<size_t>(1,m_panel->bank.size()));}
        auto* selector=dynamic_cast<juceRmlUi::ElemKnob*>(findChild("PatchSelect"));
        m_updatingPatchSelect=true;
        selector->setMaxValue(static_cast<float>(count));selector->setValue(static_cast<float>(selected+1),false);
        m_updatingPatchSelect=false;
        const auto number=juce::String(selected+1).paddedLeft('0',2).toStdString();
        auto* display=findChild("PatchNumber");
        display->SetInnerRML(m_panel->failed?"Er":number);
        display->SetProperty("opacity",m_panel->loading && (juce::Time::getMillisecondCounter()/250)%2?"0.4":"1");
        if(status!=m_status)
        {
            m_status=status;
            findChild("PanelStatus")->SetInnerRML(Rml::StringUtilities::EncodeRml(status+" · Esc: Settings"));
            m_processor.getController().onStateLoaded();
        }
        const char* names[]{"Knob1","Knob2","Knob3"};
        for(unsigned i=0;i<3;++i)
        {
            const bool mapped=(m_panel->knobMask.load()&(1u<<i))!=0;
            auto* knob=findChild(names[i]);
            knob->SetAttribute("tooltip",mapped?std::string("Assignable knob ")+std::to_string(i+1):"No assignment in this patch");
            knob->SetProperty(Rml::PropertyId::PointerEvents,mapped?Rml::Style::PointerEvents::Auto:Rml::Style::PointerEvents::None);
        }
        findChild("Patch")->SetAttribute("tooltip",m_panel->button4Mapped.load()?
            "Patch Decrement; with Shift on, use assigned Button 4":"Patch Decrement; no Button 4 assignment in this patch");
    }
}
