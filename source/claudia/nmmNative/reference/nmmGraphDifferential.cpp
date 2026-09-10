// Full fixed-101 module graph versus the captured compiled DSP program.
// Resident interrupt/DMA and MCU delivery latency are outside this test.
#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_graph.h"
#include "graph101_oracle.h"
#include "../mcu/nord101_mcu_contract.h"
#include "nmmLib/nmmrom.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dsp56k;
using namespace nmm::native;
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
std::vector<uint32_t> load(const std::string& path,const char* hash) {
    std::ifstream file(path,std::ios::binary);
    require(bool(file),"Cannot read "+path);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    require(bytes.size()==0x20000*3 && nmm::sha256(bytes)==hash,"Fixture size/hash mismatch: "+path);
    std::vector<uint32_t> words(bytes.size()/3);
    for(size_t i=0;i<words.size();++i)
        words[i]=(uint32_t(bytes[i*3])<<16)|(uint32_t(bytes[i*3+1])<<8)|bytes[i*3+2];
    return words;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2 || (argc==3 && std::string(argv[2])=="--jit") || (argc==4 && std::string(argv[2])=="--emit-board"),
                "usage: nmmGraphDifferential CAPTURE_DIRECTORY [--jit | --emit-board FILE]");
        const bool jit=argc==3;
        const bool emit=argc==4;
        std::ofstream board;
        if(emit) {board.open(argv[3]);require(bool(board),"Cannot write board fixture");
            board<<"// Generated from captured DSP program execution, validated against native.\n#pragma once\n#include <cstdint>\nnamespace nmm::native::graph101_oracle {\nstruct ExpectedBlock { uint64_t audio, state; };\nconstexpr ExpectedBlock expected[] = {\n";}
        const std::string dir=argv[1];
        const auto x=load(dir+"/warmup_dsp_x.bin","b803f5c4e4d43ccf40bf970c89f25b798be4123066564263402a98c144a7be90");
        const auto y=load(dir+"/warmup_dsp_y.bin","77c27b3a5623a25cf2b754222f80cddec18fa22e498056e3f64bb507093428b1");
        const auto p=load(dir+"/warmup_dsp_p.bin","289eedd1a5dbfef5941c2d94e0f645a5d4d7512b7b723f7588d3ee163cb7f257");
        Logging::setLogFunc([](const std::string&){});
        DefaultMemoryValidator validator;
        PeripheralsNop px,py;
        Memory memory(validator,0x20000,0x200000,0x200000);
        DSP dsp(memory,&px,&py);
        // Stop before resident sample interrupt and before its restore epilogue.
        for(unsigned i=0x175;i<0x2ff;++i) memory.set(MemArea_P,i,p[i]);
        memory.set(MemArea_P,0x1ba,0x0c01ba);
        memory.set(MemArea_P,0x2ff,0x0c02ff);
        dsp.getJit().notifyProgramMemWrite(0x2ff);
        auto config=dsp.getJit().getConfig();
        config.maxInstructionsPerBlock=16;config.dynamicFastInterrupts=true;
        config.linkJitBlocks=false;dsp.getJit().setConfig(config);
        Graph101 graph;graph.initialize(x.data(),y.data());
        for(unsigned i=0;i<Graph101::words;++i) {
            memory.set(MemArea_X,i,x[i]);memory.set(MemArea_Y,i,y[i]);
        }
        auto& regs=dsp.regs();
        regs.sr.var=0;regs.a.var=0;regs.b.var=0;regs.x.var=0;regs.y.var=0;
        for(unsigned i=0;i<8;++i) {regs.r[i].var=0;regs.n[i].var=0;regs.m[i].var=0xffffff;}
        regs.n[2].var=0x7bc;regs.n[5].var=0x780;
        auto run=[&](unsigned start,unsigned end) {
            dsp.setPC(start);
            if(jit) dsp.getJit().checkModeChange();
            unsigned calls=0;
            while(dsp.getPC().toWord()!=end && calls++<1024) {
                if(jit) dsp.execJit();else dsp.exec();
            }
            require(dsp.getPC().toWord()==end,"DSP graph failed to reach boundary");
        };
        auto compare=[&](unsigned frame,const char* phase) {
            auto& mem=graph.memory();
            require(!mem.fault,"Native memory bounds fault at frame "+std::to_string(frame)+" "+phase);
            for(unsigned i=0;i<Graph101::words;++i) {
                if(mem.x[i]!=memory.get(MemArea_X,i)||mem.y[i]!=memory.get(MemArea_Y,i)) {
                    std::cerr<<"frame="<<std::dec<<frame<<" phase="<<phase<<" address="<<std::hex<<i
                             <<" X native="<<mem.x[i]<<" dsp="<<memory.get(MemArea_X,i)
                             <<" Y native="<<mem.y[i]<<" dsp="<<memory.get(MemArea_Y,i)<<'\n';
                    throw std::runtime_error("Graph state mismatch");
                }
            }
            const auto& c=graph.cursor();
            require((uint64_t(c.a)<<8)==uint64_t(regs.a.var) && (uint64_t(c.b)<<8)==uint64_t(regs.b.var),"Graph accumulator mismatch");
            require(c.r3==regs.r[3].var && c.r4==regs.r[4].var,"Graph allocation cursor mismatch");
        };
        // Sweep every MIDI note with four velocities and both gate edges.
        // Control phase is explicit here; this is not a firmware latency claim.
        constexpr unsigned frames=graph101_oracle::frames;
        uint64_t audioHash=graph101_oracle::initialHash;
        for(unsigned frame=0;frame<frames;++frame) {
            if(graph101_oracle::hasEvent(frame)) {
                const auto e=graph101_oracle::event(frame);
                graph.setVoiceWords(e.pitch,e.velocity,e.gate);
                memory.set(MemArea_X,0xf,e.pitch);memory.set(MemArea_X,0xd,e.velocity);memory.set(MemArea_X,0xe,e.gate);
            }
            if((frame%4)==0) {
                regs.r[3].var=0x73;regs.r[4].var=0x6a;
                regs.x.var=(uint64_t(regs.x.var)&0xffffff000000ull)|memory.get(MemArea_X,0xf);
                run(0x175,0x1ba);graph.control();compare(frame,"control");
            }
            const auto output=memory.get(MemArea_X,4);
            memory.set(MemArea_Y,output,0);memory.set(MemArea_Y,output+1,0);
            regs.r[3].var=0x60;regs.r[4].var=0x60;
            run(0x1df,0x2ff);graph.sample();compare(frame,"sample");
            audioHash=graph101_oracle::hashAudio(audioHash,{memory.get(MemArea_Y,output),memory.get(MemArea_Y,output+1)});
            if((frame%graph101_oracle::framesPerBlock)==graph101_oracle::framesPerBlock-1) {
                uint64_t stateHash=graph101_oracle::initialHash;
                for(unsigned i=0;i<Graph101::words;++i) stateHash=filter_f92_oracle::mixWord(stateHash,memory.get(MemArea_X,i));
                for(unsigned i=0;i<Graph101::words;++i) stateHash=filter_f92_oracle::mixWord(stateHash,memory.get(MemArea_Y,i));
                if(emit) board<<"{0x"<<std::hex<<audioHash<<"ull,0x"<<stateHash<<"ull},\n";
                audioHash=graph101_oracle::initialHash;
            }
        }
        if(emit) {board<<"};\n}\n";require(bool(board),"Board fixture write failed");}
        std::cout<<"PASS full compiled 101 module graph: "<<frames<<" frames, all 128 notes, four velocities, gate edges, oracle="<<(jit?"JIT":"interpreter")<<'\n';
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
