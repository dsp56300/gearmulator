#include "nmmDevice.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    try
    {
        const bool realtimeFour=argc==4 && std::string(argv[3])=="--realtime-four";
        require(argc==3 || realtimeFour,"Usage: nmmDeviceTests firmware.bin 101.pch | firmware.bin FourVoices.pch --realtime-four");
        auto panel=std::make_shared<nmmJucePlugin::PanelState>();
        std::ifstream input(argv[2]); require(bool(input),"Missing fixture");
        panel->patchText.assign(std::istreambuf_iterator<char>(input),{});
        panel->offline=!realtimeFour;
        nmmJucePlugin::Device device({},argv[1],panel);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!panel->ready && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {std::lock_guard<std::mutex> lock(panel->mutex);require(panel->ready,panel->status.c_str());}
        require(panel->knobMask==(realtimeFour?7u:3u),"Fixture knob mappings");
        std::array<float,128> left{},right{};
        synthLib::TAudioOutputs out{};out[0]=left.data();out[1]=right.data();
        synthLib::TAudioInputs in{};
        std::vector<synthLib::SMidiEvent> midi,response;
        double sum=0,squares=0,tailSum=0,tailSquares=0;
        unsigned count=0,tailCount=0;
        const auto playbackStart=std::chrono::steady_clock::now();
        for(unsigned block=0;block<3750;++block)
        {
            if(realtimeFour && (block==600 || block==1600))
            {
                std::vector<uint8_t> edit{0xf0,0x33,0x4c,6,1,64,1,4,0,uint8_t(block==600?110:127)};
                unsigned checksum=0;for(auto b:edit) checksum+=b;edit.push_back(checksum&127);edit.push_back(0xf7);
                require(panel->editor->receive(edit.data(),edit.size()),"Concurrent editor MIDI queue");
            }
            if(realtimeFour && block==1500) require(panel->values[1]==110,"Concurrent editor change reached native voice parameters");
            midi.clear();
            if(block==0) midi.emplace_back(synthLib::MidiEventSource::Host,0x90,60,100,32);
            if(block==3000) midi.emplace_back(synthLib::MidiEventSource::Host,0x80,60,0,32);
            if(realtimeFour && (block==0 || block==3000))
                for(auto note:{64,67,71}) midi.emplace_back(synthLib::MidiEventSource::Host,block==0?0x90:0x80,note,block==0?100:0,32);
            device.process(in,out,128,midi,response);
            for(auto v:right)
            {
                require(std::isfinite(v),"Non-finite output");
                require(std::abs(v)<1.01f,"DAC output range");
                if(block>=750 && block<2250) {sum+=v;squares+=double(v)*v;++count;}
                if(block>=3500) {tailSum+=v;tailSquares+=double(v)*v;++tailCount;}
            }
            if(realtimeFour) std::this_thread::sleep_until(playbackStart+std::chrono::microseconds(uint64_t(block+1)*128*1000000/96000));
        }
        const auto ac=std::sqrt(std::max(0.0,squares/count-std::pow(sum/count,2)));
        const auto tail=std::sqrt(std::max(0.0,tailSquares/tailCount-std::pow(tailSum/tailCount,2)));
        require(ac>(realtimeFour?.02:.005) && ac<(realtimeFour?.2:.05) && tail<ac*.01,"Decoded DAC worker note/release audio");
        require(panel->droppedJobs==0 && panel->underruns==0,"Worker queue continuity");
        panel->values[1]=70;
        std::vector<uint8_t> state;
        require(device.getState(state,synthLib::StateTypeGlobal),"Save state");
        require(state[3]==5 && state.size()>0x100000,"Version 5 includes native flash and separate working patch");
        size_t flashOffset=10;
        auto skipString=[&]() {uint32_t size=0;for(unsigned i=0;i<4;++i) size|=uint32_t(state[flashOffset+i])<<(8*i);flashOffset+=4+size;};
        for(unsigned i=0;i<state[9]*3u;++i) skipString();
        const auto legacyEnd=flashOffset;
        const auto flashEnd=flashOffset+4+0x100000;
        auto truncated=state;truncated.pop_back();require(!device.setState(truncated,synthLib::StateTypeGlobal),"Reject truncated flash state");
        state[flashEnd-1]=0x5a; // unused tail byte: verify flash survives worker reboot

        panel->values[1]=20;
        require(device.setState(state,synthLib::StateTypeGlobal),"Restore state");
        const auto restoredDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!panel->ready && std::chrono::steady_clock::now()<restoredDeadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        {std::lock_guard<std::mutex> lock(panel->mutex); if(!panel->ready || panel->values[1]!=70) throw std::runtime_error("Restore knob after worker compilation: " + panel->status + " value=" + std::to_string(panel->values[1].load()));}
        std::vector<uint8_t> again;require(device.getState(again,synthLib::StateTypeGlobal),"Save restored flash");
        nmmJucePlugin::PanelState decoded;require(nmmJucePlugin::applyPanelState(again,decoded),"Decode restored state");
        require(decoded.flash.back()==0x5a,"Flash contents survive host state restoration");
        auto legacy=state;legacy.resize(legacyEnd);legacy[3]=3;
        require(device.setState(legacy,synthLib::StateTypeGlobal),"Legacy version 3 remains readable");
        std::cout<<"PASS worker: MIDI offsets, audio/release, queue continuity, patch/knob state; AC="<<ac<<" realtime_four="<<realtimeFour<<'\n';
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
