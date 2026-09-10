#include "nmmDevice.h"
#include "nmmEditorMidi.h"
#include <condition_variable>
#include <fstream>
#include <iostream>
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace
{
    void require(bool value,const char* text) {if(!value) throw std::runtime_error(text);}
    class Replies final : public juce::MidiInputCallback
    {
    public:
        void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage& m) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            messages.emplace_back(m.getRawData(),m.getRawData()+m.getRawDataSize());changed.notify_all();
        }
        bool waitFor(uint8_t command)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock,std::chrono::seconds(3),[&]{for(const auto& m:messages) if(m.size()>4 && m[0]==0xf0 && m[2]==command) return true;return false;});
        }
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::vector<uint8_t>> messages;
    };
}
int main(int argc,char** argv)
{
    try
    {
        const bool serve=argc==4 && std::string(argv[3])=="--serve";
        require(argc==3 || serve,"usage: nmmEditorMidiTests firmware FourVoices.pch [--serve]");
        juce::ScopedJuceInitialiser_GUI juceInit;
        auto panel=std::make_shared<nmmJucePlugin::PanelState>();
        std::ifstream file(argv[2]);require(bool(file),"Patch fixture");
        panel->patchText.assign(std::istreambuf_iterator<char>(file),{});panel->patchName="FourVoices";
        panel->bank.push_back({panel->patchName,panel->patchText});
        panel->bank.push_back({"Second patch",panel->patchText});
        nmmJucePlugin::Device device({},argv[1],panel);
        nmmJucePlugin::EditorMidi bridge(panel->editor);
        std::string ports;{std::lock_guard<std::mutex> lock(panel->editor->mutex);ports=panel->editor->ports;}
        Replies replies;std::unique_ptr<juce::MidiInput> input;std::unique_ptr<juce::MidiOutput> output;
        for(const auto& d:juce::MidiInput::getAvailableDevices()) if(d.name.toStdString()==ports+" PC Out") input=juce::MidiInput::openDevice(d.identifier,&replies);
        for(const auto& d:juce::MidiOutput::getAvailableDevices()) if(d.name.toStdString()==ports+" PC In") output=juce::MidiOutput::openDevice(d.identifier);
        require(input && output,"Instance virtual MIDI ports are discoverable");input->start();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!panel->ready && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(panel->ready,"Emulator startup");
        if(serve)
        {
            std::cout << "READY " << ports << std::endl;
            uint64_t previous=0;
            for(unsigned i=0;i<1200;++i)
            {
#if JUCE_MAC
                CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.1,true);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
                std::lock_guard<std::mutex> lock(panel->editor->mutex);
                if(panel->editor->received!=previous)
                {
                    previous=panel->editor->received;std::cout << "Received editor messages " << previous << std::endl;
                    std::lock_guard<std::mutex> replyLock(replies.mutex);
                    for(const auto& message:replies.messages) {std::cout << "TX";for(auto b:message) std::cout << ' ' << std::hex << unsigned(b);std::cout << std::dec << std::endl;}
                    replies.messages.clear();
                }
            }
            input->stop();return 0;
        }
        const uint8_t hello[]{0xf0,0x33,0,6,0,3,3,0xf7};output->sendMessageNow(juce::MidiMessage(hello,sizeof hello));
        require(replies.waitFor(0),"Native discovery over CoreMIDI with stopped audio host");
        const uint8_t queryId[]{0xf0,0x33,0x5c,6,65,53,123,0xf7};
        output->sendMessageNow(juce::MidiMessage(queryId,sizeof queryId));
        require(replies.waitFor(0x58),"Native patch ID query");
        uint8_t pid=0;
        {std::lock_guard<std::mutex> lock(replies.mutex);for(const auto& m:replies.messages) if(m.size()==9 && m[2]==0x58 && m[5]==0x36) pid=m[6];}
        std::vector<uint8_t> edit{0xf0,0x33,0x4c,6,pid,64,1,4,0,80};
        unsigned sum=0;for(auto b:edit) sum+=b;edit.push_back(sum&127);edit.push_back(0xf7);
        output->sendMessageNow(juce::MidiMessage(edit.data(),int(edit.size())));
        const auto editDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!panel->editor->revision.load() && std::chrono::steady_clock::now()<editDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(panel->editor->revision.load()>0,"Editor edit received");
        std::vector<uint8_t> state;require(device.getState(state,synthLib::StateTypeGlobal),"Snapshot waits for native editor edit");
        {std::lock_guard<std::mutex> lock(panel->mutex);require(!panel->patchNative.empty(),"Working editor patch stored in host state");}
        require(panel->values[1]==80,"Native edit updates front-panel knob");
        auto waitReady=[&]
        {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!panel->ready && std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            {std::lock_guard<std::mutex> lock(panel->mutex);if(!panel->ready) throw std::runtime_error("Patch selection startup: "+panel->status);}
        };
        require(panel->selectPatch(1),"Select second front-panel patch");waitReady();
        require(panel->values[1]==127,"Second patch has its own parameter values");
        require(panel->selectPatch(0),"Return to edited patch");waitReady();
        require(panel->values[1]==127,"Front-panel patch selection restores the saved program");
        auto damaged=state;
        const auto packet=std::find(damaged.begin(),damaged.end(),0xf0);
        auto end=std::find(packet,damaged.end(),0xf7);
        require(end!=damaged.end(),"Saved native packet framing");
        *(end-1)^=1;require(!device.setState(damaged,synthLib::StateTypeGlobal),"Reject corrupted native snapshot checksum");
        require(device.setState(state,synthLib::StateTypeGlobal),"Restore version 5 host state");
        const auto restoreDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!panel->ready && std::chrono::steady_clock::now()<restoreDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(panel->ready && panel->values[1]==80,"Edited native knob value survives host state round trip");
        auto manager=[&](std::initializer_list<uint8_t> data)
        {
            {std::lock_guard<std::mutex> lock(replies.mutex);replies.messages.clear();}
            std::vector<uint8_t> message{0xf0,0x33,0x5c,6};message.insert(message.end(),data);
            unsigned checksum=0;for(auto b:message) checksum+=b;message.push_back(checksum&127);message.push_back(0xf7);
            output->sendMessageNow(juce::MidiMessage(message.data(),int(message.size())));
        };
        auto waitLibrary=[&](auto predicate)
        {
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(std::chrono::steady_clock::now()<end)
            {
                {std::lock_guard<std::mutex> lock(panel->mutex);if(predicate()) return;}
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            throw std::runtime_error("Library synchronization timed out");
        };
        manager({65,20,0,0});require(replies.waitFor(0x58),"Imported patch list response");
        {std::lock_guard<std::mutex> lock(replies.mutex);const auto& bytes=replies.messages.back();
         const std::string name="FourVoices";require(std::search(bytes.begin(),bytes.end(),name.begin(),name.end())!=bytes.end(),"Imported patch name appears over MIDI");}
        manager({65,11,0,0,98});
        waitLibrary([&]{return panel->bank.size()==99 && panel->bank[98].name=="FourVoices" && panel->bank[98].text.empty();});
        std::vector<uint8_t> libraryState;require(device.getState(libraryState,synthLib::StateTypeGlobal),"Save sparse flash library state");
        {
            auto migrated=std::make_shared<nmmJucePlugin::PanelState>();
            {std::lock_guard<std::mutex> lock(panel->mutex);migrated->flash=panel->flash;migrated->bank.assign(panel->bank.begin(),panel->bank.begin()+2);migrated->patchText=panel->patchText;migrated->patchName=panel->patchName;}
            nmmJucePlugin::Device legacy({},argv[1],migrated);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            bool found=false;
            while(std::chrono::steady_clock::now()<deadline)
            {
                {std::lock_guard<std::mutex> lock(migrated->mutex);found=migrated->ready && migrated->bank.size()==99 && migrated->bank[98].name=="FourVoices";}
                if(found) break;std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(found,"Existing flash-only saves populate the panel without resaving");
        }
        require(panel->selectPatch(2),"Panel skips empty positions toward native slot 99");waitReady();
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->selectedPatch==98,"Panel retains native slot number");}
        require(panel->values[1]==80,"Native saved slot retains edited output level");
        require(panel->selectPatch(97),"Panel skips empty positions in reverse");waitReady();
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->selectedPatch==1,"Reverse selection finds previous occupied slot");}
        manager({65,10,0,0,98});
        waitLibrary([&]{return panel->ready && panel->selectedPatch==98;});
        require(replies.waitFor(0x58),"Browser load acknowledgement after compilation");
        require(panel->values[1]==80,"Browser load uses same saved slot as panel");
        require(device.setState(libraryState,synthLib::StateTypeGlobal),"Restore sparse library state");waitReady();
        require(panel->selectPatch(98),"Restored native slot selectable");waitReady();
        require(panel->values[1]==80,"Flash slot survives host state round trip");
        manager({65,20,0,96});require(replies.waitFor(0x58),"Last patch list page");
        {std::lock_guard<std::mutex> lock(replies.mutex);const auto& bytes=replies.messages.back();
         const std::string name="FourVoices";require(std::search(bytes.begin(),bytes.end(),name.begin(),name.end())!=bytes.end(),"Slot 99 name appears in final list page");}
        manager({65,12,0,98,0});
        waitLibrary([&]{return panel->bank[98].name.empty();});
        require(panel->selectPatch(0),"Other patches remain after native deletion");waitReady();
        manager({65,12,0,1,0});
        waitLibrary([&]{return panel->bank[1].name.empty();});
        std::string reimport;{std::lock_guard<std::mutex> lock(panel->mutex);reimport=panel->bank[0].text+"\n";}
        panel->requestPatch(reimport,"Reimport");waitReady();
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->selectedPatch==0 && panel->bank[1].name.empty() && panel->patchName=="Reimport","Patch Load replaces only the working patch");}
        std::cout<<"PASS shared library: imported names, native save to slot 99, panel/browser load, sparse host state and delete\n";
        input->stop();
        std::cout << "PASS virtual MIDI: discoverable ports, stopped-host firmware handshake, live edit, native host-state round trip\n";
    }
    catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
