#include "nmmLib/nmmcapture.h"
#include <fstream>
#include "nmmPluginProcessor.h"
#include "nmmEditor.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <iostream>

class StandaloneSmoke final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override {return "NMM standalone smoke";}
    const juce::String getApplicationVersion() override {return "1";}
    void initialise(const juce::String&) override
    {
        // Read a snapshot of the shipped standalone settings; never overwrite it.
        const auto args=getCommandLineParameterArray();
        allowRecovery=args.contains("--recover");
        rapidTrigger=args.contains("--rapid-trigger");
        fourVoices=args.contains("--four-voices") || rapidTrigger;
        manualCapture=args.contains("--manual-capture");
        for(const auto& arg:args)
        {
            if(arg.startsWith("--capture=")) {capturePath=arg.fromFirstOccurrenceOf("=",false,false).toStdString();capture=std::make_unique<nmm::Capture>();}
            if(arg.startsWith("--hold-seconds=")) holdTicks=juce::jlimit(30,3000,arg.fromFirstOccurrenceOf("=",false,false).getIntValue()*10);
            if(arg.startsWith("--slot=")) triggerSlot=arg.fromFirstOccurrenceOf("=",false,false).getIntValue();
            if(arg.startsWith("--voices=")) triggerVoices=juce::jlimit(1,4,arg.fromFirstOccurrenceOf("=",false,false).getIntValue());
            if(arg.startsWith("--release=")) triggerRelease=juce::jlimit(0,127,arg.fromFirstOccurrenceOf("=",false,false).getIntValue());
            if(arg.startsWith("--audio-capture=")) audioPath=arg.fromFirstOccurrenceOf("=",false,false).toStdString();
            if(arg.startsWith("--block=")) requestedBlock=arg.fromFirstOccurrenceOf("=",false,false).getIntValue();
        }
        if(args.size()>=1)
            if(auto xml=juce::parseXML(juce::File(args[0]))) settings.restoreFromXml(*xml);
        window=std::make_unique<juce::StandaloneFilterWindow>("NMM standalone smoke",juce::Colours::black,&settings,false);
        window->setVisible(true);
        auto* holder=window->getPluginHolder();
        if(!holder->processor->checkBusesLayoutSupported(holder->processor->getBusesLayout()))
        {finish(false,"Standalone output bus layout rejected");return;}
        if(requestedBlock)
        {
            auto setup=holder->deviceManager.getAudioDeviceSetup();setup.sampleRate=44100;setup.bufferSize=requestedBlock;
            const auto error=holder->deviceManager.setAudioDeviceSetup(setup,false);
            if(error.isNotEmpty()) {finish(false,error.toRawUTF8());return;}
        }
        auto* device=holder->deviceManager.getCurrentAudioDevice();
        if(!device) {finish(false,"Standalone did not open an audio device");return;}
        std::cout<<"Standalone output: "<<device->getName()<<" rate="<<device->getCurrentSampleRate()<<" block="<<device->getCurrentBufferSizeSamples()<<std::endl;
        if(requestedBlock && (device->getCurrentBufferSizeSamples()!=requestedBlock || device->getCurrentSampleRate()!=44100))
        {finish(false,"Audio device did not accept requested format");return;}
        if(fourVoices)
        {
            auto* processor=dynamic_cast<nmmJucePlugin::Processor*>(holder->processor.get());
            unsigned selected=~0u;
            {std::lock_guard<std::mutex> lock(processor->panel()->mutex);
                for(unsigned i=0;i<processor->panel()->bank.size();++i)
                    if(processor->panel()->bank[i].name=="FourVoices") selected=i;}
            if(selected==~0u) {finish(false,"FourVoices fixture missing from bank");return;}
            if(rapidTrigger && triggerSlot) {
                if(audioPath.empty()) {finish(false,"Rapid trigger requires --audio-capture=path");return;}
                processor->panel()->selectPatch(triggerSlot-1,true);holdTicks=600;
            } else if(rapidTrigger) {
                std::string text;{std::lock_guard<std::mutex> lock(processor->panel()->mutex);text=processor->panel()->bank[selected].text;}
                const auto header=text.find("0 127 0 127 2 0 0 4 4000");
                if(header==std::string::npos || audioPath.empty()) {finish(false,"Rapid trigger requires FourVoices and --audio-capture=path");return;}
                text[header+18]=char('0'+triggerVoices);processor->panel()->requestPatch(text,"Trigger probe");
                holdTicks=600;
            } else processor->panel()->selectPatch(selected,true);
        }
        meter=holder->deviceManager.getOutputLevelGetter();
        startTimer(rapidTrigger?20:100);
    }
    void shutdown() override {
        stopTimer();window.reset(); // Joins the emulator worker before accessing capture.
        if(capture) {std::ofstream out(capturePath);capture->write(out);if(!out) {std::cerr<<"Capture export failed\n";setApplicationReturnValue(1);}}
    }
private:
    void finish(bool success,const char* message)
    {
        std::cout<<(success?"PASS ":"FAIL ")<<message<<std::endl;
        setApplicationReturnValue(success?0:1);stopTimer();
        juce::MessageManager::callAsync([this]{window.reset();quit();});
    }
    void timerCallback() override
    {
        auto* processor=dynamic_cast<nmmJucePlugin::Processor*>(window->getAudioProcessor());
        if(!processor) {finish(false,"Unexpected processor");return;}
        ++ticks;
        if(processor->panel()->failed)
        {
            std::string status;
            {std::lock_guard<std::mutex> lock(processor->panel()->mutex);status=processor->panel()->status;}
            if(allowRecovery && !recovered)
            {
                auto* editor=processor->getEditorState()->getEditor();
                if(editor->findChild("PatchNumber")->GetInnerRML()!="Er") return;
                auto* context=editor->getDocument()->GetContext();
                juceRmlUi::RmlInterfaces::ScopedAccess access(*editor->getRmlComponent());
                auto point=[&](const char* id)
                {
                    context->Update();auto* element=editor->findChild(id);
                    const auto offset=element->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto size=element->GetBox().GetSize(Rml::BoxArea::Border);
                    context->ProcessMouseMove(int(offset.x+size.x/2),int(offset.y+size.y/2),0);
                };
                unsigned count;
                {std::lock_guard<std::mutex> lock(processor->panel()->mutex);count=static_cast<unsigned>(processor->panel()->bank.size());}
                point("Shift");context->ProcessMouseButtonDown(0,0);context->ProcessMouseButtonUp(0,0);
                point("PatchSelect");
                for(unsigned i=0;i<count;++i) context->ProcessMouseWheel(1,0);
                context->ProcessMouseButtonUp(0,0);
                if(editor->findChild("PatchNumber")->GetInnerRML()!="01") {finish(false,"Could not select 01 after failed restore");return;}
                std::cout<<"Recovered to bank 01 via Patch Select; saved patch retained, error="<<status<<std::endl;
                recovered=true;ticks=0;return;
            }
            finish(false,status.c_str());return;
        }
        if(processor->panel()->underruns || processor->panel()->droppedJobs) {finish(false,"Standalone lost audio/jobs");return;}
        if(ticks>holdTicks+200) {finish(false,"Standalone test timed out");return;}
        if(ticks%10==0) std::cout<<"tick="<<ticks<<" ready="<<processor->panel()->ready<<" output="<<meter->getCurrentLevel()<<" underruns="<<processor->panel()->underruns<<" drops="<<processor->panel()->droppedJobs<<std::endl;
        if(!processor->panel()->ready) return;
        if(rapidTrigger) {
            if(stage==0 && ticks>=100) {
                if(triggerRelease>=0) processor->panel()->values[3]=triggerRelease;stage=1;stageTick=ticks;
            } else if(stage==1 && ticks-stageTick>=25) {
                processor->startAudioCapture(uint32_t(processor->getSampleRate()*12),true);
                triggerStart=juce::Time::getMillisecondCounterHiRes();stage=2;stageTick=ticks;
            } else if(stage==2) {
                auto* editor=processor->getEditorState()->getEditor();
                juceRmlUi::RmlInterfaces::ScopedAccess access(*editor->getRmlComponent());
                auto* context=editor->getDocument()->GetContext();
                const auto elapsed=ticks-stageTick;
                if(elapsed%5==1 && triggerCount<64) {
                    if(triggerCount==0) {
                        auto* shift=editor->findChild("Shift");const auto shiftPos=shift->GetAbsoluteOffset(Rml::BoxArea::Border);const auto shiftSize=shift->GetBox().GetSize(Rml::BoxArea::Border);
                        context->ProcessMouseMove(int(shiftPos.x+shiftSize.x/2),int(shiftPos.y+shiftSize.y/2),0);
                        context->ProcessMouseButtonDown(0,0);context->ProcessMouseButtonUp(0,0);
                    }
                    auto* button=editor->findChild("NoteTrig");const auto pos=button->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=button->GetBox().GetSize(Rml::BoxArea::Border);
                    context->ProcessMouseMove(int(pos.x+size.x/2),int(pos.y+size.y/2),0);
                    triggerTimes[triggerCount++]=juce::Time::getMillisecondCounterHiRes()-triggerStart;
                    context->ProcessMouseButtonDown(0,0);
                } else if(elapsed%5==4) context->ProcessMouseButtonUp(0,0);
                if(elapsed>=325) {
                    const auto result=processor->stopAudioCapture(audioPath);
                    std::ofstream times(audioPath+".csv");times<<"# voices="<<triggerVoices<<" release="<<triggerRelease<<" sample_rate="<<result.sampleRate<<" frames="<<result.frames<<'\n';
                    for(unsigned i=0;i<triggerCount;++i) times<<i<<','<<triggerTimes[i]-triggerTimes[0]<<'\n';
                    finish(result.started && triggerCount==64,"rapid Note Trig audio capture complete");
                }
            }
            return;
        }
        if(manualCapture)
        {
            if(!capture) {finish(false,"--manual-capture requires --capture=path");return;}
            if(stage==0 && ticks>=30) {processor->panel()->capture=capture.get();stage=1;stageTick=ticks;std::cout<<"Manual capture armed: play now"<<std::endl;}
            else if(stage==1 && ticks-stageTick>=holdTicks) finish(true,"manual capture complete (no automatic audio assertion)");
            return;
        }
        auto* editor=processor->getEditorState()->getEditor();
        auto* context=editor->getDocument()->GetContext();
        juceRmlUi::RmlInterfaces::ScopedAccess access(*editor->getRmlComponent());
        if(stage==0 && ticks>=30)
        {
            processor->panel()->capture=capture.get();
            auto* button=editor->findChild("NoteTrig");
            const auto offset=button->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size=button->GetBox().GetSize(Rml::BoxArea::Border);
            auto* shift=editor->findChild("Shift");
            const auto shiftOffset=shift->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto shiftSize=shift->GetBox().GetSize(Rml::BoxArea::Border);
            context->ProcessMouseMove(int(shiftOffset.x+shiftSize.x/2),int(shiftOffset.y+shiftSize.y/2),0);
            context->ProcessMouseButtonDown(0,0);context->ProcessMouseButtonUp(0,0);
            context->ProcessMouseMove(int(offset.x+size.x/2),int(offset.y+size.y/2),0);
            context->ProcessMouseButtonDown(0,0);
            if(fourVoices) for(int key:{64,67,71}) processor->addMidiEvent({synthLib::MidiEventSource::Editor,0x90,uint8_t(key),100});
            stage=1;stageTick=ticks;
        }
        else if(stage==1 && ticks-stageTick>=holdTicks)
        {
            const auto level=meter->getCurrentLevel();
            context->ProcessMouseButtonUp(0,0);
            if(fourVoices) for(int key:{64,67,71}) processor->addMidiEvent({synthLib::MidiEventSource::Editor,0x80,uint8_t(key),0});
            if(level<0.0025) {finish(false,"Standalone Note Trig produced no audible output");return;}
            stage=2;stageTick=ticks;
        }
        else if(stage==2 && ticks-stageTick>=20)
            finish(meter->getCurrentLevel()<0.0001f,"standalone wrapper, audio device and Note Trig/release");
    }
    bool rapidTrigger=false;unsigned triggerVoices=4,triggerCount=0,triggerSlot=0;int triggerRelease=-1;
    double triggerStart=0;std::array<double,64> triggerTimes{};std::string audioPath;
    std::unique_ptr<nmm::Capture> capture;std::string capturePath;bool manualCapture=false;
    juce::PropertySet settings;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
    juce::AudioDeviceManager::LevelMeter::Ptr meter;
    unsigned ticks=0,stage=0,stageTick=0;
    bool allowRecovery=false,recovered=false,fourVoices=false;
    int requestedBlock=0;unsigned holdTicks=30;
};
START_JUCE_APPLICATION(StandaloneSmoke)
