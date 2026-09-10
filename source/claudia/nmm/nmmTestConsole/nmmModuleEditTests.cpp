#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include "nmmLib/nmmrom.h"
#include "model/ModuleDescriptions.h"
#include "model/BitStream.h"
#include "protocol/NewModuleMessage.h"
#include "protocol/NewCableMessage.h"
#include "protocol/DeleteCableMessage.h"
#include "protocol/DeleteModuleMessage.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using Bytes=std::vector<uint8_t>;
void require(bool condition,const std::string& reason) {if(!condition) throw std::runtime_error(reason);}
Bytes frame(uint8_t command,Bytes payload)
{
    Bytes result{0xf0,0x33,command,6};result.insert(result.end(),payload.begin(),payload.end());
    unsigned sum=0;for(auto b:result) sum+=b;
    result.push_back(sum&127);result.push_back(0xf7);return result;
}
struct Session
{
    nmm::Hardware hw;
    uint8_t pid=0;
    Bytes partial;
    std::vector<Bytes> replies;
    unsigned deferrals=0;
    bool pendingControl=false;
    explicit Session(std::shared_ptr<const nmm::Rom> rom,bool reference):hw(std::move(rom))
    {
        if(!reference) {hw.setAudioDrivenExecution(64,false);hw.setDeadlineLinkedJit(true);}
        hw.boot(30000000);
        nmm::Patch patch;
        for(uint16_t area=0;area<2;++area)
        {
            patch.modules.push_back({area,1,7,0,0,"Source"});
            patch.modules.push_back({area,2,4,1,0,"Output"});
            patch.modules.push_back({area,3,9,2,0,"Slave"});
            patch.parameters.push_back({area,1,0,64});
            patch.parameters.push_back({area,1,1,64});
            patch.parameters.push_back({area,2,0,100});
            patch.cables.push_back({area,{2,0,1,64,0,0}});
        }
        hw.loadPatch(patch,30000000);
        hw.receiveEditorMidi();
        exchange(frame(0x5c,{65,53}),false);
    }
    void tick(bool controls)
    {
        if(controls && pendingControl)
        {
            if(hw.prepareControlUpdate()) {hw.setMasterVolume(100);pendingControl=false;}
            else ++deferrals;
        }
        const auto audio=hw.render(128,1000000);
        // Integer exponent check remains effective with the project's fast-math flags.
        for(const auto& f:audio) for(float sample:f)
        {uint32_t bits;std::memcpy(&bits,&sample,4);require((bits&0x7f800000)!=0x7f800000,"non-finite audio");}
        for(auto b:hw.receiveEditorMidi())
        {
            if(b>=0xf8) continue;
            if(b==0xf0) partial.clear();
            partial.push_back(b);
            if(b!=0xf7) continue;
            require(partial.size()>=7,"short reply");
            unsigned sum=0;for(size_t i=0;i+2<partial.size();++i) sum+=partial[i];
            require((sum&127)==partial[partial.size()-2],"reply checksum");
            const auto cc=partial[2]&0x7c;
            if(cc==0x58) pid=partial[4];
            if(cc==0x50 && partial[5]==0x38 && partial.size()>=10) pid=partial[7];
            require(!(cc==0x50 && partial[5]==0x7e),"firmware rejected edit, code="+std::to_string(partial[6]));
            replies.push_back(partial);partial.clear();
        }
    }
    // Wait for the native transaction to settle, retaining all replies. The
    // next operation uses the PID from this transaction, just like the editor.
    void exchange(const Bytes& request,bool controls=true,bool expectsReply=true)
    {
        replies.clear();pendingControl=controls;
        require(hw.sendEditorMidi(request.data(),request.size()),"input queue full");
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<1125;++i)
        {
            tick(controls);
            if(hw.editorIdle() && (!expectsReply || !replies.empty())) return;
            if(std::chrono::steady_clock::now()-start>std::chrono::seconds(15)) break;
        }
        throw std::runtime_error("editor transaction did not settle");
    }
    Bytes query(uint8_t subcommand,uint8_t area)
    {
        exchange(frame(0x5c,{pid,subcommand,area}),false);
        Bytes data;
        for(const auto& reply:replies)
            if((reply[2]&0x70)==0x70) data.insert(data.end(),reply.begin()+5,reply.end()-2);
        require(!data.empty(),"missing patch section reply");return data;
    }
    bool hasModule(int area,int index,int type)
    {
        BitStream bits(query(75,uint8_t(area)));
        require(bits.readBits(8)==74,"module section type");
        require(bits.readBits(1)==unsigned(area),"module area");
        const auto count=bits.readBits(7);
        bool found=false;
        for(unsigned i=0;i<count;++i)
        {
            const auto t=bits.readBits(7),idx=bits.readBits(7);
            bits.readBits(7);bits.readBits(7);
            if(idx==unsigned(index)) {require(t==unsigned(type),"module type changed");found=true;}
        }
        return found;
    }
    unsigned cableCount(int area)
    {
        BitStream bits(query(83,uint8_t(area)));
        require(bits.readBits(8)==82,"cable section type");
        require(bits.readBits(1)==unsigned(area),"cable area");return bits.readBits(15);
    }
    void verifyParameter(int area,int parameter,int value)
    {
        // The same native module record inspected by Hardware::editorKnobs.
        const auto address=0x175f40+(area?0x4c66:0x467a)+4*8;
        uint32_t record=0;for(unsigned i=0;i<4;++i) record=(record<<8)|hw.readMemory('C',address+i);
        require(record>=0x165c30 && record<0x169c30,"missing native parameter record");
        require(hw.readMemory('C',record+0x29+8*parameter)==unsigned(value),"parameter edit did not reach firmware");
    }
};
void exercise(Session& session,const ModuleDescriptor& module,int area)
{
    auto& s=session;
    std::vector<int> params;
    for(const auto& p:module.parameters) if(p.paramClass=="parameter") params.push_back(p.defaultValue);
    // Match PatchSynchronizer::onModuleAdded, including its empty CustomDump.
    NewModuleMessageProto add(s.pid,module.index,area,8,3,0,"UnderTest",params,{});
    s.exchange(add.toSysEx(0));
    require(s.hasModule(area,8,module.index),"module add not reflected in native patch");
    const auto cablesBefore=s.cableCount(area);
    int dest=0,destPort=0,source=0,sourcePort=0;SignalType color=SignalType::Audio;
    // Attach the tested module to the baseline graph using a compatible source
    // or destination, including the special oscillator master/slave cable.
    for(const auto& port:module.connectors) if(port.isOutput)
    {
        dest=port.signalType==SignalType::MasterSlave?3:2;
        destPort=dest==3?0:1;source=8;sourcePort=port.index;color=port.signalType;break;
    }
    if(!dest) for(const auto& port:module.connectors) if(!port.isOutput)
    {dest=8;destPort=port.index;source=1;sourcePort=port.signalType==SignalType::MasterSlave?1:0;color=port.signalType;break;}
    if(dest)
    {
        s.exchange(NewCableMessage(s.pid,area,color,dest,false,destPort,source,true,sourcePort).toSysEx(0));
        require(s.cableCount(area)==cablesBefore+1,"cable add not reflected in native patch");
    }
    for(const auto& p:module.parameters) if(p.paramClass=="parameter")
    {
        const int value=p.defaultValue<p.maxValue?p.defaultValue+1:p.minValue;
        s.exchange(frame(0x4c,{s.pid,64,uint8_t(area),8,uint8_t(p.index),uint8_t(value)}),true,false);
        s.verifyParameter(area,p.index,value);
    }
    if(dest)
    {
        s.exchange(DeleteCableMessage(s.pid,area,color,dest,false,destPort,source,true,sourcePort).toSysEx(0));
        require(s.cableCount(area)==cablesBefore,"cable delete not reflected in native patch");
    }
    s.exchange(DeleteModuleMessage(s.pid,area,8).toSysEx(0));
    require(!s.hasModule(area,8,module.index),"module delete not reflected in native patch");
    require(s.cableCount(area)==cablesBefore,"module edit changed baseline cables");
    require(s.deferrals>0,"pending automation never exercised deferral");
}
}
int main(int argc,char** argv)
{
    try
    {
        require(argc>=3 && argc<=5,"usage: nmmModuleEditTests firmware modules.xml [module-id|all] [--reference]");
        const int only=argc>=4 && std::string(argv[3])!="all"?std::stoi(argv[3]):-1;
        const bool reference=argc==5 && std::string(argv[4])=="--reference";
        require(argc!=5 || reference,"unknown execution mode");
        ModuleDescriptions descriptions;
        require(descriptions.loadFromFile(juce::File(juce::String(argv[2]))),"module descriptions");
        auto rom=std::make_shared<const nmm::Rom>(argv[1]);
        unsigned passed=0,failed=0;
        for(const auto& module:descriptions.getAllModules())
        {
            if(!module.instantiable || (only>=0 && module.index!=only)) continue;
            for(int area:{0,1})
            {
                const auto label=std::to_string(module.index)+" "+module.name.toStdString()+" area="+std::to_string(area);
                std::cout<<"START "<<label<<std::endl;
                try
                {
                    Session session(rom,reference);exercise(session,module,area);
                    ++passed;std::cout<<"PASS "<<label<<std::endl;
                }
                catch(const std::exception& e) {++failed;std::cout<<"FAIL "<<label<<": "<<e.what()<<std::endl;}
            }
        }
        std::cout<<"RESULT passed="<<passed<<" failed="<<failed<<std::endl;
        return !passed || failed?1:0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
