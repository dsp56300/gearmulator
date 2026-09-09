#include "nmmPluginProcessor.h"
#include "nmmEditor.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Input.h"
#include "synthLib/romLoader.h"
#include "synthLib/plugin.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>
#include <cmath>
#include <stdexcept>

class UiSmoke final : public juce::JUCEApplication,private juce::Timer,private juce::AudioIODeviceCallback
{
public:
    const juce::String getApplicationName() override {return "NMM UI smoke";}
    const juce::String getApplicationVersion() override {return "1";}
    void initialise(const juce::String&) override
    {
        const auto args=getCommandLineParameterArray();
        if(args.size()<2 || args.size()>3) {fail("Usage: nmmUiSmoke firmware-directory output-prefix [--realtime]");return;}
        realtime=args.size()==3 && args[2]=="--realtime";
        prefix=args[1];
        synthLib::RomLoader::addSearchPath(args[0].toStdString());
        processor=std::make_unique<nmmJucePlugin::Processor>();
        auto& config=processor->getConfig();
        previousScale=config.getValue("scale");hadScale=config.containsKey("scale");restoreScale=true;
        const auto old=config.getValue("forceSoftwareRenderer");const bool existed=config.containsKey("forceSoftwareRenderer");
        config.setValue("forceSoftwareRenderer",1);
        config.setValue("scale",100);
        ui.reset(processor->createEditor());
        std::cerr<<"initial ui "<<ui->getWidth()<<"x"<<ui->getHeight()<<" document "<<processor->getEditorState()->getWidth()<<"x"<<processor->getEditorState()->getHeight()<<" rootScale "<<processor->getEditorState()->getRootScale()<<std::endl;
        if(existed) config.setValue("forceSoftwareRenderer",old);else config.removeValue("forceSoftwareRenderer");
        ui->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        ui->setTopLeftPosition(80,80);ui->setVisible(true);
        if(realtime)
        {
            const auto error=devices.initialise(0,2,nullptr,true);
            if(error.isNotEmpty()) {fail(error.toRawUTF8());return;}
            auto* device=devices.getCurrentAudioDevice();
            if(!device) {fail("No active audio output device");return;}
            std::cerr<<"Live audio device: "<<device->getName()<<" rate="<<device->getCurrentSampleRate()<<" block="<<device->getCurrentBufferSizeSamples()<<std::endl;
            player.setProcessor(processor.get());
            devices.addAudioCallback(this);
        }
        startTimer(100);
    }
    void shutdown() override {stopTimer();stopAudio();ui.reset();restoreConfig();processor.reset();}
private:
    void restoreConfig()
    {
        if(!processor || !restoreScale) return;
        if(hadScale) processor->getConfig().setValue("scale",previousScale);
        else processor->getConfig().removeValue("scale");
        restoreScale=false;
    }
    void stopAudio()
    {
        devices.removeAudioCallback(this);player.setProcessor(nullptr);devices.closeAudioDevice();
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {player.audioDeviceAboutToStart(device);}
    void audioDeviceStopped() override {player.audioDeviceStopped();}
    void audioDeviceIOCallbackWithContext(const float* const* input,int ins,float* const* output,int outs,int samples,const juce::AudioIODeviceCallbackContext& context) override
    {
        player.audioDeviceIOCallbackWithContext(input,ins,output,outs,samples,context);
        double sum=0,squares=0;float peak=0;
        if(outs>1) for(int i=0;i<samples;++i)
        {
            const auto v=output[1][i];sum+=v;squares+=double(v)*v;peak=std::max(peak,std::abs(v));
        }
        liveAc=std::sqrt(std::max(0.0,squares/samples-std::pow(sum/samples,2)));livePeak=peak;
        ++callbacks;
    }
    void finish()
    {
        stopTimer();
        stopAudio();
        // Remove the desktop peer and release the plugin before stopping AppKit.
        ui.reset();restoreConfig();processor.reset();quit();
    }
    void fail(const char* why) {std::cerr<<why<<std::endl;setApplicationReturnValue(1);finish();}
    void escape(jucePluginEditorLib::Editor& editor)
    {
        Rml::Dictionary data;data["key_identifier"]=int(Rml::Input::KI_ESCAPE);
        editor.getDocument()->DispatchEvent(Rml::EventId::Keydown,data);
    }
    void capture(jucePluginEditorLib::Editor& editor,const char* suffix,int next)
    {
        const auto image=editor.getRmlComponent()->createComponentSnapshot(editor.getRmlComponent()->getLocalBounds());
        auto stream=juce::File(prefix+suffix).createOutputStream();
        if(!stream || !stream->setPosition(0) || stream->truncate().failed() || !juce::PNGImageFormat().writeImageToStream(image,*stream)) {fail("Screenshot failed");return;}
        stage=next;
    }
    double renderNoteWindow()
    {
        juce::AudioBuffer<float> audio(2,128);
        juce::MidiBuffer midi;
        double sum=0,squares=0;
        unsigned count=0;
        // Offline host callback: no injected host MIDI. Only the UI can trigger audio.
        for(unsigned block=0;block<512;++block)
        {
            midi.clear();
            static_cast<juce::AudioProcessor&>(*processor).processBlock(audio,midi);
            for(int i=0;i<audio.getNumSamples();++i)
            {
                const double value=audio.getSample(1,i);
                if(!std::isfinite(value)) throw std::runtime_error("Non-finite Note Trig audio");
                if(block>=384) {sum+=value;squares+=value*value;++count;}
            }
        }
        return std::sqrt(std::max(0.0,squares/count-std::pow(sum/count,2)));
    }
    void pointAt(jucePluginEditorLib::Editor& editor,const char* id)
    {
        auto* element=editor.findChild(id);
        auto* context=editor.getDocument()->GetContext();
        context->Update();
        const auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size=element->GetBox().GetSize(Rml::BoxArea::Border);
        context->ProcessMouseMove(int(offset.x+size.x/2),int(offset.y+size.y/2),0);
        if(context->GetHoverElement()!=element) throw std::runtime_error("Panel control hit test failed");
    }
    void toggleShift(jucePluginEditorLib::Editor& editor)
    {
        pointAt(editor,"Shift");
        auto* context=editor.getDocument()->GetContext();
        context->ProcessMouseButtonDown(0,0);context->ProcessMouseButtonUp(0,0);
    }
    void pressPatchButton(jucePluginEditorLib::Editor& editor,const char* id)
    {
        unsigned before=0;
        {std::lock_guard<std::mutex> lock(processor->panel()->mutex);before=processor->panel()->selectedPatch;}
        pointAt(editor,id);
        auto* context=editor.getDocument()->GetContext();
        context->ProcessMouseButtonDown(0,0);context->ProcessMouseButtonUp(0,0);
        unsigned after=0;
        {std::lock_guard<std::mutex> lock(processor->panel()->mutex);after=processor->panel()->selectedPatch;}
        if(after==before) throw std::runtime_error(std::string(id)+" did not select another patch");
    }
    void turnPatch(jucePluginEditorLib::Editor& editor,float delta,const char* expected)
    {
        pointAt(editor,"PatchSelect");
        editor.getDocument()->GetContext()->ProcessMouseWheel(delta,0);
        if(editor.findChild("PatchNumber")->GetInnerRML()!=expected)
            throw std::runtime_error("Patch Select did not immediately update LED");
    }
    void testNoteTrigger(jucePluginEditorLib::Editor& editor)
    {
        processor->setNonRealtime(true);
        static_cast<juce::AudioProcessor&>(*processor).prepareToPlay(48000,128);
        auto* button=editor.findChild("NoteTrig");
        auto* context=editor.getDocument()->GetContext();
        const auto offset=button->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size=button->GetBox().GetSize(Rml::BoxArea::Border);
        auto press=[&](int mouseButton)
        {
            juceRmlUi::RmlInterfaces::ScopedAccess access(*editor.getRmlComponent());
            context->ProcessMouseMove(int(offset.x+size.x/2),int(offset.y+size.y/2),0);
            if(context->GetHoverElement()!=button) throw std::runtime_error("Note Trig hit test failed");
            context->ProcessMouseButtonDown(mouseButton,0);
        };
        auto release=[&]
        {
            juceRmlUi::RmlInterfaces::ScopedAccess access(*editor.getRmlComponent());
            context->ProcessMouseButtonUp(0,0);
        };
        // Exercise the actual button and Processor routing with rapid same-note
        // retriggers. No host MIDI injection can bypass the editor event path.
        std::string fixture;
        {std::lock_guard<std::mutex> lock(processor->panel()->mutex);fixture=processor->panel()->bank.at(3).text;}
        juce::AudioBuffer<float> rapidAudio(2,128);juce::MidiBuffer rapidMidi;
        toggleShift(editor);
        for(unsigned voices:{1u,2u,4u}) {
            auto text=fixture;const auto header=text.find("0 127 0 127 2 0 0 4 4000");
            if(header==std::string::npos) throw std::runtime_error("Unexpected FourVoices header");
            text[header+18]=char('0'+voices);
            processor->panel()->requestPatch(text,"Trigger probe "+std::to_string(voices));renderNoteWindow();
            for(unsigned releaseValue:{0u,10u,20u,30u}) {
                processor->panel()->values[3]=releaseValue;renderNoteWindow();
                double minimum=1,maximum=0;unsigned quiet=0;
                for(unsigned repeat=0;repeat<64;++repeat) {
                    press(0);double squares=0;
                    for(unsigned block=0;block<32;++block) {
                        rapidMidi.clear();static_cast<juce::AudioProcessor&>(*processor).processBlock(rapidAudio,rapidMidi);
                        if(block>=16) for(int i=0;i<128;++i) squares+=std::pow(rapidAudio.getSample(1,i),2);
                    }
                    release();
                    for(unsigned block=0;block<2;++block) {rapidMidi.clear();static_cast<juce::AudioProcessor&>(*processor).processBlock(rapidAudio,rapidMidi);}
                    const auto level=std::sqrt(squares/(16*128));minimum=std::min(minimum,level);maximum=std::max(maximum,level);quiet+=level<.0025;
                }
                renderNoteWindow();
                std::cout<<"TRIGGER voices="<<voices<<" release="<<releaseValue<<" minimum="<<minimum<<" maximum="<<maximum<<" quiet="<<quiet<<std::endl;
                if(releaseValue==0 && quiet) throw std::runtime_error("Rapid zero-release Note Trig missed a press");
            }
        }
        processor->panel()->requestPatch(fixture,"FourVoices");renderNoteWindow();
        processor->panel()->values[3]=0;renderNoteWindow();
        if(renderNoteWindow()>1e-7) throw std::runtime_error("Initial patch is not silent");
        toggleShift(editor);
        for(const auto mode:{0,1,2,3})
        {
            toggleShift(editor);
            press(0);
            const double held=renderNoteWindow();
            if(held<0.0025) throw std::runtime_error("Note Trig press did not render audible audio");
            if(mode==0) release();
            if(mode==1) {context->ProcessMouseMove(10,10,0);release();}
            if(mode==2) escape(editor);
            if(mode==3) ui.reset(); // Skin survives host editor closure.
            const double tail=renderNoteWindow();
            if(tail>held*.01) throw std::runtime_error("Note Trig release left a held note");
            if(mode==0 || mode==1) toggleShift(editor);
            std::cout<<"PASS Note Trig "<<mode<<" held="<<held<<" release="<<tail<<std::endl;
        }
        static_cast<juce::AudioProcessor&>(*processor).releaseResources();
    }
    void timerCallback() override
    {
        if(++ticks>200) {std::cerr<<"stage="<<stage<<" ready="<<processor->panel()->ready.load()<<std::endl;fail("UI smoke timed out");return;}
        auto* editor=processor->getEditorState()->getEditor();
        if(!editor || !editor->getDocument() || !processor->panel()->ready) return;
        if(realtime)
        {
            if(ticks%10==0) std::cerr<<"live tick="<<ticks<<" callbacks="<<callbacks.load()<<" AC="<<liveAc.load()<<" peak="<<livePeak.load()<<" underruns="<<processor->panel()->underruns<<" drops="<<processor->panel()->droppedJobs<<std::endl;
            if(stage==0 && ticks>=30)
            {
                auto* button=editor->findChild("NoteTrig");
                const auto offset=button->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto size=button->GetBox().GetSize(Rml::BoxArea::Border);
                auto* context=editor->getDocument()->GetContext();
                juceRmlUi::RmlInterfaces::ScopedAccess access(*editor->getRmlComponent());
                auto* shift=editor->findChild("Shift");
                const auto shiftOffset=shift->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto shiftSize=shift->GetBox().GetSize(Rml::BoxArea::Border);
                context->ProcessMouseMove(int(shiftOffset.x+shiftSize.x/2),int(shiftOffset.y+shiftSize.y/2),0);
                context->ProcessMouseButtonDown(0,0);
                context->ProcessMouseButtonUp(0,0);
                context->ProcessMouseMove(int(offset.x+size.x/2),int(offset.y+size.y/2),0);
                context->ProcessMouseButtonDown(0,0);
                stage=1;settingsTick=ticks;
            }
            else if(stage==1 && ticks-settingsTick>=30)
            {
                if(liveAc<0.0025) {fail("Live Note Trig produced no audible signal");return;}
                editor->getDocument()->GetContext()->ProcessMouseButtonUp(0,0);
                stage=2;settingsTick=ticks;
            }
            else if(stage==2 && ticks-settingsTick>=20)
            {
                if(liveAc>1e-7) {fail("Live Note Trig did not release");return;}
                std::cout<<"PASS live audio-device Note Trig/release"<<std::endl;finish();
            }
            return;
        }
        if(stage==0)
        {
            for(const char* id:{"Shift","MasterVolume","Knob1","Knob2","Knob3","NoteTrig","Patch","PatchNumber"})
                if(!editor->findChild(id,false)) {fail("Missing panel control");return;}
            if(ticks<15) return;
            if(ui->getWidth()!=1335 || ui->getHeight()!=783) {fail("Unexpected default panel dimensions (expected 1335 x 783)");return;}
            std::cerr<<"capture ui "<<ui->getWidth()<<"x"<<ui->getHeight()<<" document "<<editor->getDefaultWidth()<<"x"<<editor->getDefaultHeight()<<" root "<<editor->getRmlComponent()->getWidth()<<"x"<<editor->getRmlComponent()->getHeight()<<std::endl;
            capture(*editor,"-panel.png",1);
        }
        else if(stage==1)
        {
            escape(*editor);if(!editor->settingsOpened()) {fail("Escape did not open shared settings");return;}
            stage=2;settingsTick=ticks;
        }
        else if(stage==2 && ticks-settingsTick>=10)
        {
            auto* toggle=editor->findChild("EditorMidiEnabled",false);
            auto* ports=editor->findChild("EditorMidiPorts",false);
            if(!toggle || !ports) {juce::File(prefix+"-settings.rml").replaceWithText(editor->getDocument()->GetInnerRML());fail("Missing device-specific editor MIDI settings");return;}
            Rml::ElementList buttons;editor->getDocument()->GetElementsByTagName(buttons,"button");
            for(auto* button:buttons) if(button->GetInnerRML()=="MIDI") button->DispatchEvent(Rml::EventId::Click,{});
            const bool enabled=processor->editorMidiEnabled();
            toggle->DispatchEvent(Rml::EventId::Click,{});
            if(processor->editorMidiEnabled()==enabled) {fail("Editor MIDI toggle did not change port state");return;}
            toggle->DispatchEvent(Rml::EventId::Click,{});
            if(processor->editorMidiEnabled()!=enabled || ports->GetInnerRML().empty()) {fail("Editor MIDI settings did not restore port state");return;}
            editor->getDocument()->GetContext()->Update();
            stage=7;settingsTick=ticks;
            std::cout<<"PASS Escape MIDI settings, virtual port toggle and instance name"<<std::endl;
        }
        else if(stage==7 && ticks-settingsTick>=3)
        {
            if(!editor->findChild("EditorMidiPorts")->IsVisible(true)) {fail("Editor MIDI settings page is not visible");return;}
            capture(*editor,"-settings.png",3);
        }
        else if(stage==3)
        {
            escape(*editor);if(editor->settingsOpened()) {fail("Escape did not close shared settings");return;}
            escape(*editor);escape(*editor);
            if(editor->settingsOpened()) {fail("Rapid Escape close failed");return;}
            stage=4;settingsTick=ticks;
        }
        else if(stage==4 && ticks-settingsTick>=5)
        {
            try
            {
                savedKnob3=processor->panel()->values[3];
                pressPatchButton(*editor,"NoteTrig");
                pressPatchButton(*editor,"Patch");
                toggleShift(*editor);
                turnPatch(*editor,-1,"02");turnPatch(*editor,-1,"03");
                if(processor->panel()->values[3]!=savedKnob3) throw std::runtime_error("Patch Select altered assigned knob 3");
                std::vector<uint8_t> state;
                if(!processor->getPlugin().getState(state,synthLib::StateTypeGlobal) || state.size()<3 || state[0]!=1 || state[2]!='N')
                    throw std::runtime_error("Patch bank state lost framework prefix");
                turnPatch(*editor,1,"02");
                if(!processor->getPlugin().setState(state)) throw std::runtime_error("Patch bank state restore failed");
                stage=5;
            }
            catch(const std::exception& error) {fail(error.what());return;}
        }
        else if(stage==5)
        {
            // Wait for the UI timer to reflect the completed restore.
            if(editor->findChild("PatchNumber")->GetInnerRML()!="03") return;
            try
            {
                {std::lock_guard<std::mutex> lock(processor->panel()->mutex);
                 if(processor->panel()->selectedPatch!=2 || processor->panel()->patchName!="BasicOsc" || processor->panel()->bank.size()!=4)
                    throw std::runtime_error("Restored bank/patch does not match LED");}
                turnPatch(*editor,1,"02");turnPatch(*editor,1,"01");toggleShift(*editor);stage=6;
                std::cout<<"PASS Shift/Patch Select, immediate LED, bank/state restore and knob isolation"<<std::endl;
            }
            catch(const std::exception& error) {fail(error.what());return;}
        }
        else if(stage==6)
        {
            try {testNoteTrigger(*editor);}
            catch(const std::exception& error) {fail(error.what());return;}
            std::cout<<"PASS panel controls, rendering, Escape settings open/close and rapid reopen"<<std::endl;finish();
        }
    }
    juce::String prefix;
    std::unique_ptr<nmmJucePlugin::Processor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> ui;
    int stage=0,ticks=0,settingsTick=0;
    int savedKnob3=0;
    juce::String previousScale;
    bool hadScale=false,restoreScale=false;
    bool realtime=false;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    std::atomic<double> liveAc{0};
    std::atomic<float> livePeak{0};
    std::atomic<unsigned> callbacks{0};
};
START_JUCE_APPLICATION(UiSmoke)
