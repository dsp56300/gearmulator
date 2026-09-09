#include "nmmDevice.h"
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
static void require(bool v,const char* m) {if(!v) throw std::runtime_error(m);}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3 || argc==4,"usage: nmmLoadingTests firmware patch-directory [--baseline|--audio-driven]");
        const bool baseline=argc==4 && std::string(argv[3])=="--baseline";
        const bool audioDriven=argc==4 && std::string(argv[3])=="--audio-driven";
        if(argc==4 && !baseline && !audioDriven) throw std::runtime_error("Unknown loading option");
        auto panel=std::make_shared<nmmJucePlugin::PanelState>();
        if(audioDriven) {panel->audioDrivenFrames=64;panel->audioDrivenLinkedJit=true;}
        for(const auto* name:{"FourVoices","101","SimpleSqr1","Gong01","BDrm","BasicOsc"})
        {
            std::ifstream f(std::string(argv[2])+"/"+name+".pch");require(bool(f),"fixture");
            panel->bank.push_back({name,std::string(std::istreambuf_iterator<char>(f),{})});
        }
        const unsigned brokenIndex=unsigned(panel->bank.size());
        panel->bank.push_back({"Broken","not a patch"});
        panel->patchText=panel->bank[0].text;panel->patchName=panel->bank[0].name;
        const auto begin=Clock::now();const auto cpu=std::clock();
        nmmJucePlugin::Device device({},argv[1],panel);
        std::array<float,128> left{},right{};synthLib::TAudioOutputs out{};out[0]=left.data();out[1]=right.data();
        synthLib::TAudioInputs in{};std::vector<synthLib::SMidiEvent> events,response;
        auto next=Clock::now();
        auto pump=[&]
        {
            device.process(in,out,128,events,response);events.clear();
            next+=std::chrono::microseconds(1333);std::this_thread::sleep_until(next);
            if(Clock::now()-next>std::chrono::milliseconds(20)) next=Clock::now();
        };
        auto ready=[&]
        {
            const auto until=Clock::now()+std::chrono::seconds(15);
            while(!panel->ready && !panel->failed && Clock::now()<until) pump();
            {std::lock_guard<std::mutex> lock(panel->mutex);if(!panel->ready) throw std::runtime_error(panel->status);}
        };
        ready();std::cout<<"startup_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-begin).count()<<'\n';
        auto select=[&](unsigned index,const char* label)
        {
            const auto start=Clock::now();const auto c=std::clock();require(panel->selectPatch(index),"select");ready();
            std::cout<<label<<" wall_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<" cpu_ms="<<1000.0*(std::clock()-c)/CLOCKS_PER_SEC<<'\n';
        };
        // Hold a note, then change patches without a host note-off. The previous
        // generation and any notes sent while loading must not leak into the new one.
        events.emplace_back(synthLib::MidiEventSource::Host,0x90,60,100,0);
        for(unsigned i=0;i<100;++i) pump();
        select(1,"cold");select(0,"return");
        double sum=0,sq=0;unsigned count=0;
        for(unsigned i=0;i<400;++i) {pump();if(i>=200) for(auto v:right) {sum+=v;sq+=v*v;++count;}}
        const auto ac=std::sqrt(std::max(0.0,sq/count-std::pow(sum/count,2)));std::cout<<"return_without_note_ac="<<ac<<'\n';
        if(!baseline) require(ac<0.0001,"Held notes leaked across patch reload");
        const auto burst=Clock::now();const auto burstCpu=std::clock();
        for(unsigned i=0;i<20;++i)
        {
            panel->selectPatch(i%2?1:2);
            for(unsigned j=0;j<8;++j) {events.emplace_back(synthLib::MidiEventSource::Host,0x90,80,100,0);pump();}
        }
        panel->selectPatch(0);ready();
        std::cout<<"burst_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-burst).count()<<" burst_cpu_ms="<<1000.0*(std::clock()-burstCpu)/CLOCKS_PER_SEC<<'\n';
        const auto started=panel->startedLoads.load();
        panel->selectPatch(3); // Gong01 has never been requested: force a cold compiler path.
        const auto cancelDeadline=Clock::now()+std::chrono::seconds(2);
        while(panel->startedLoads==started && Clock::now()<cancelDeadline) pump();
        // Do not assume an earlier rapid-selection request was coalesced: on
        // a fast worker it may already have populated the warm cache.
        require(panel->startedLoads>started,"Cold cancellation load did not start");
        panel->selectPatch(0);ready();
        if(!baseline) require(panel->cancelledLoads>0,"Superseded compilation was not cancelled");
        panel->selectPatch(brokenIndex);
        const auto deadline=Clock::now()+std::chrono::seconds(15);while(!panel->failed && Clock::now()<deadline) pump();
        require(panel->failed,"Invalid patch must fail explicitly");select(0,"recovery");
        std::cout<<"underruns="<<panel->underruns<<" dropped="<<panel->droppedJobs<<" total_cpu_ms="<<1000.0*(std::clock()-cpu)/CLOCKS_PER_SEC<<'\n';
        for(const unsigned index:{2u,3u,4u,5u}) select(index,"cold_matrix");
        for(const unsigned index:{0u,4u,3u,2u,1u,5u,0u})
        {
            select(index,"warm_matrix");
            events.emplace_back(synthLib::MidiEventSource::Host,0x90,60,100,0);
            double energy=0;
            for(unsigned i=0;i<100;++i)
            {
                pump();for(auto v:right) {require(std::isfinite(v) && std::abs(v)<=1,"Warm patch produced invalid audio");energy+=v*v;}
            }
            require(energy>0.00001,"Warm patch produced no audio");
            events.emplace_back(synthLib::MidiEventSource::Host,0x80,60,0,0);pump();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto idleCpu=std::clock();std::this_thread::sleep_for(std::chrono::seconds(3));
        const auto idlePercent=100.0*(std::clock()-idleCpu)/CLOCKS_PER_SEC/3;
        std::cout<<"idle_cpu_percent="<<idlePercent<<" cold="<<panel->coldLoads<<" warm="<<panel->warmLoads<<" cancelled="<<panel->cancelledLoads<<'\n';
        // Resume after the worker has entered its quiet sleep. The first note
        // must retain the normal scheduling delay without an initial underrun.
        next=Clock::now();events.emplace_back(synthLib::MidiEventSource::Host,0x90,60,100,0);
        double resumeEnergy=0;
        for(unsigned i=0;i<200;++i) {pump();for(auto value:right) resumeEnergy+=double(value)*value;}
        events.emplace_back(synthLib::MidiEventSource::Host,0x80,60,0,0);pump();
        require(resumeEnergy>0.00001,"Stopped-host resume lost its note");
        if(!baseline) {require(panel->underruns==0 && panel->droppedJobs==0,"Loading queued obsolete playback jobs");require(idlePercent<5,"Idle hardware kept rendering unnecessarily");}
        auto closing=std::make_shared<nmmJucePlugin::PanelState>();closing->patchText=panel->bank[0].text;
        auto loadingDevice=std::make_unique<nmmJucePlugin::Device>(synthLib::DeviceCreateParams{},argv[1],closing);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto closeStart=Clock::now();loadingDevice.reset();
        const auto closeMs=std::chrono::duration<double,std::milli>(Clock::now()-closeStart).count();
        std::cout<<"close_during_load_ms="<<closeMs<<'\n';if(!baseline) require(closeMs<200,"Shutdown waited for an obsolete boot");
        std::cout<<"PASS loading, rapid selection, cancellation, idle/resume and failure recovery\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
