// Stable-patch JIT address capture. Sampling runs externally after READY.
#include "nmmLib/nmmhardware.h"
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <stdexcept>
int main(int argc,char** argv)
{
    try
    {
        if(argc<5) throw std::runtime_error("usage: nmmJitProfile firmware patch output-prefix seconds [--continuations] [--deadline-graphs] [--block-limit 16|32|64]");
        const unsigned seconds=std::stoul(argv[4]);if(seconds<10 || seconds>300) throw std::runtime_error("Use 10..300 seconds");
        bool regions=false;bool candidate=false;unsigned blockLimit=16;
        for(int i=5;i<argc;++i) {
            const std::string arg=argv[i];
            if((arg=="--deadline-graphs" || arg=="--sample-regions")) regions=true;
            else if(arg=="--continuations") candidate=true;
            else if(arg=="--block-limit" && i+1<argc) blockLimit=std::stoul(argv[++i]);
            else throw std::runtime_error("Unknown option");
        }
        const auto loadStart=std::chrono::steady_clock::now();
        nmm::Hardware hw(argv[1]);hw.setJitBlockLimit(blockLimit,regions);hw.setAudioDrivenExecution(64,false);hw.setDeadlineLinkedJit(true,candidate);hw.boot(30000000);hw.loadPatch(argv[2],30000000);hw.setMasterVolume(127);
        const auto loadSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-loadStart).count();
        std::array<std::array<float,2>,128> audio{};
        for(auto key:{60,64,67,71}) hw.sendMidi(0x90,key,100);
        for(unsigned b=0;b<1500;++b) hw.renderInto(audio.data(),128,1000000);
        auto snapshot=[&](const char* suffix)
        {
            std::ostringstream map;
            std::ofstream native;
            if(std::string(suffix)==".before.map")
            {
                native.open(std::string(argv[3])+".native.s");
                if(!native) throw std::runtime_error("Cannot write native JIT assembly");
            }
            hw.dumpJitMap(map,native.is_open()?&native:nullptr);
            std::ofstream file(std::string(argv[3])+suffix);if(!file) throw std::runtime_error("Cannot write JIT map");file<<map.str();return map.str();
        };
        const auto before=snapshot(".before.map");
        std::cout<<"READY"<<std::endl;
        const auto start=std::chrono::steady_clock::now();
        const auto cpuStart=std::clock();
        for(unsigned b=0;b<seconds*750;++b)
        {
            hw.renderInto(audio.data(),128,1000000);
            std::this_thread::sleep_until(start+std::chrono::microseconds(uint64_t(b+1)*128000000/96000));
        }
        const auto cpuSeconds=double(std::clock()-cpuStart)/CLOCKS_PER_SEC;
        const auto wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"MEASURE block_limit="<<blockLimit<<" effective_block_limit="<<(regions?16:blockLimit)<<" deadline_graphs="<<regions<<" load_seconds="<<loadSeconds<<" continuations="<<candidate<<" cpu_seconds="<<cpuSeconds<<" wall_seconds="<<wallSeconds<<" cpu_percent="<<100*cpuSeconds/wallSeconds<<std::endl;
        const auto after=snapshot(".after.map");
        if(before!=after) throw std::runtime_error("JIT map changed during sampling; attribution is unsafe");
        std::cout<<"PASS stable JIT map capture\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
