// Offline firmware evidence collection. No callbacks, logging hooks or DSP changes.
#include "nmmLib/nmmhardware.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

int main(int argc,char** argv)
{
    try {
        if(argc!=4) throw std::runtime_error("usage: nmmArchitectureInspect firmware patch output-prefix");
        nmm::Hardware hw(argv[1]);
        hw.setAudioDrivenExecution(64,false);hw.setDeadlineLinkedJit(true);
        hw.boot(30000000);
        auto snapshot=[&](const std::string& phase) {
            const std::string prefix=std::string(argv[3])+"-"+phase;
            const auto before=hw.timing();
            std::ofstream state(prefix+".state");hw.dumpState(state);
            std::ofstream map(prefix+".map");hw.dumpJitMap(map);
            for(char space:{'C','P','X','Y'}) {
                std::ofstream file(prefix+"-"+space+".bin",std::ios::binary);
                const unsigned count=space=='C'?0x200000:0x20000;
                for(unsigned a=0;a<count;++a) {
                    const auto word=hw.readMemory(space,a);
                    if(space!='C') {file.put(char(word>>16));file.put(char(word>>8));}
                    file.put(char(word));
                }
                if(!file) throw std::runtime_error("Snapshot write failed");
            }
            const auto after=hw.timing();
            if(!state || !map || before.cycles!=after.cycles || before.frames!=after.frames)
                throw std::runtime_error("Snapshot failed or advanced emulation");
            std::cout<<"SNAPSHOT "<<phase<<" cycles="<<after.cycles<<" frames="<<after.frames<<std::endl;
        };
        snapshot("boot");
        const std::string patchPath=argv[2];
        if(patchPath.size()>=4 && patchPath.substr(patchPath.size()-4)==".syx") {
            std::ifstream file(patchPath,std::ios::binary);
            if(!file) throw std::runtime_error("Native snapshot unavailable");
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
            hw.initializePatch(30000000);hw.restoreEditorPatch(bytes);
        } else hw.loadPatch(patchPath,30000000);
        hw.setMasterVolume(100);
        snapshot("ready");
        std::array<std::array<float,2>,128> audio{};
        for(auto key:{60,64,67,71}) hw.sendMidi(0x90,key,100);
        for(unsigned b=0;b<1500;++b) hw.renderInto(audio.data(),128,1000000);
        snapshot("held");
        for(auto key:{60,64,67,71}) hw.sendMidi(0x80,key,0);
        for(unsigned b=0;b<750;++b) hw.renderInto(audio.data(),128,1000000);
        snapshot("released");
        std::cout<<"PASS quiescent architecture snapshots\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
