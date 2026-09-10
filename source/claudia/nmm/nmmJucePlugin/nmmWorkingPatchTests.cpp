#include "nmmDevice.h"
#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using Bytes=std::vector<uint8_t>;
using Clock=std::chrono::steady_clock;
void require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
Bytes frame(uint8_t command,Bytes payload)
{
    Bytes message{0xf0,0x33,command,6};message.insert(message.end(),payload.begin(),payload.end());
    unsigned checksum=0;for(auto b:message) checksum+=b;
    message.push_back(checksum&127);message.push_back(0xf7);return message;
}
struct Rig
{
    std::shared_ptr<nmmJucePlugin::PanelState> panel;
    nmmJucePlugin::Device device;
    Bytes partial;
    std::vector<Bytes> replies;
    Rig(const std::string& firmware,std::shared_ptr<nmmJucePlugin::PanelState> state)
        :panel(std::move(state)),device({},firmware,panel) {ready();}
    template<class Predicate> void wait(Predicate predicate,const char* reason)
    {
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(Clock::now()<deadline)
        {
            {std::lock_guard<std::mutex> lock(panel->mutex);if(predicate()) return;
             if(panel->failed) throw std::runtime_error(panel->status);}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error(reason);
    }
    void ready() {wait([&]{return panel->ready.load();},"Startup/load timed out");}
    void collect()
    {
        Bytes bytes;
        {std::lock_guard<std::mutex> lock(panel->editor->mutex);bytes.swap(panel->editor->output);}
        for(auto b:bytes)
        {
            if(b>=0xf8) continue;
            if(b==0xf0) partial.clear();
            partial.push_back(b);
            if(b==0xf7) {replies.push_back(partial);partial.clear();}
        }
    }
    void send(const Bytes& message)
    {
        collect();replies.clear();
        require(panel->editor->receive(message.data(),message.size()),"Editor receive queue full");
    }
    template<class Predicate> Bytes response(Predicate predicate)
    {
        const auto deadline=Clock::now()+std::chrono::seconds(15);
        while(Clock::now()<deadline)
        {
            collect();for(const auto& reply:replies) if(predicate(reply)) return reply;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("Missing editor reply");
    }
    uint8_t pid()
    {
        send(frame(0x5c,{65,53}));
        return response([](const Bytes& r){return r.size()==9 && r[2]==0x58 && r[5]==0x36;})[6];
    }
    Bytes snapshot()
    {
        Bytes state;require(device.getState(state,synthLib::StateTypeGlobal),"Save host state");return state;
    }
    void restore(const Bytes& state) {require(device.setState(state,synthLib::StateTypeGlobal),"Restore host state");ready();}
    void select(unsigned index) {require(panel->selectPatch(index,true),"Select saved program");ready();}
    void upload(const std::vector<Bytes>& packets)
    {
        for(const auto& packet:packets)
        {
            send(packet);
            response([](const Bytes& r){return r.size()>=8 && r[2]==0x58 && (r[5]==0x36 || r[5]==0x7f);});
        }
        snapshot();
    }
    void store(unsigned position,const std::string& name)
    {
        std::cout<<"STORE position="<<position<<" name="<<name<<std::endl;
        send(frame(0x5c,{65,11,0,0,uint8_t(position)}));
        try {wait([&]{return panel->bank.size()>position && panel->bank[position].name==name && panel->bank[position].text.empty();},"Store did not update destination");}
        catch(...)
        {
            collect();
            for(const auto& reply:replies) {std::cerr<<"reply:";for(auto b:reply) std::cerr<<' '<<std::hex<<unsigned(b);std::cerr<<std::dec<<'\n';}
            std::lock_guard<std::mutex> lock(panel->mutex);
            std::cerr<<"status="<<panel->status<<" bank-size="<<panel->bank.size()<<" destination="<<(panel->bank.size()>position?panel->bank[position].name:"missing")<<'\n';
            throw;
        }
        snapshot();
    }
};
}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3,"usage: nmmWorkingPatchTests firmware FourVoices.pch");
        std::ifstream file(argv[2]);require(bool(file),"Patch fixture");
        const std::string text(std::istreambuf_iterator<char>(file),{});
        auto panel=std::make_shared<nmmJucePlugin::PanelState>();
        panel->bank={{"FourVoices",text,{}},{"Second",text,{}}};
        panel->patchText=text;panel->patchName="FourVoices";
        Rig rig(argv[1],panel);rig.snapshot();
        Bytes savedOriginal;
        {std::lock_guard<std::mutex> lock(panel->mutex);savedOriginal=panel->bank[0].native;}
        require(!savedOriginal.empty(),"Saved patch cache initialized before editing");
        auto unchanged=[&]
        {
            std::lock_guard<std::mutex> lock(panel->mutex);
            require(panel->bank[0].name=="FourVoices" && panel->bank[0].text==text && panel->bank[0].native==savedOriginal,"Working patch overwrote saved bank entry");
        };
        rig.send(frame(0x4c,{rig.pid(),64,1,4,0,80}));
        rig.wait([&]{return panel->values[1]==80;},"Parameter edit did not arrive");
        const auto editedState=rig.snapshot();unchanged();
        rig.select(1);rig.select(0);
        require(panel->values[1]==127,"Switching programs stored unsaved edits");
        rig.restore(editedState);unchanged();
        require(panel->values[1]==80,"Host state did not restore independent working edit");
        rig.store(98,"FourVoices");unchanged();

        // Exactly the native multipart replacement used by editor New/Open.
        nmm::Hardware blank(argv[1]);blank.boot(30000000);
        nmm::Patch empty;empty.name="Blank";blank.loadPatch(empty,30000000);
        const auto packets=blank.exportEditorPatch();
        rig.upload(packets);
        rig.wait([&]{return panel->patchName=="Blank" && panel->knobMask==0;},"New patch did not replace working buffer");
        unchanged();
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->bank[98].name=="FourVoices","New patch overwrote stored flash entry");}
        const auto blankState=rig.snapshot();
        rig.select(98);require(panel->values[1]==80,"Stored patch changed after New");
        rig.restore(blankState);
        rig.wait([&]{return panel->patchName=="Blank" && panel->knobMask==0;},"Blank working patch lost on state restore");
        unchanged();rig.select(0);require(panel->values[1]==127,"Original saved patch lost after New/state restore");

        panel->requestPatch(text,"Opened file");rig.ready();rig.snapshot();unchanged();
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->bank.size()==99 && panel->bank[1].name=="Second" && panel->patchName=="Opened file","File Open changed saved bank");}
        rig.upload(packets);rig.store(0,"Blank");
        rig.select(1);rig.select(0);
        require(panel->knobMask==0,"Explicit Store did not replace selected saved patch");
        rig.select(98);require(panel->values[1]==80,"Store changed another memory location");
        rig.send(frame(0x5c,{65,12,0,98,0}));
        rig.wait([&]{return panel->bank[98].name.empty();},"Delete did not clear saved entry");
        const auto deletedState=rig.snapshot();rig.restore(deletedState);
        require(panel->values[1]==80,"Deleting saved location also lost independent working patch");
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->bank[98].name.empty(),"Working snapshot resurrected deleted bank entry");}

        // Version 4 ends after the flash image; migrate its selected bank entry
        // to a working copy while retaining all saved data.
        auto legacy=editedState;size_t cursor=10;
        auto skip=[&] {uint32_t size=0;for(unsigned i=0;i<4;++i) size|=uint32_t(legacy[cursor+i])<<(8*i);cursor+=4+size;};
        for(unsigned i=0;i<legacy[9]*3u+1;++i) skip();
        legacy.resize(cursor);legacy[3]=4;
        nmmJucePlugin::PanelState migrated;
        require(nmmJucePlugin::applyPanelState(legacy,migrated),"Version 4 migration");
        require(migrated.patchNative==migrated.bank[0].native && migrated.bank[0].native==savedOriginal,"Legacy migration lost saved patch");
        auto invalid=editedState;invalid.back()=128;
        require(!nmmJucePlugin::applyPanelState(invalid,migrated),"Invalid working button value accepted");
        invalid=editedState;invalid[invalid.size()-2]=2;
        require(!nmmJucePlugin::applyPanelState(invalid,migrated),"Invalid working origin accepted");
        std::cout<<"PASS independent working patch: edits, New/Open, Store, bank selection, deleted location, host state and legacy migration\n";
    }
    catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
