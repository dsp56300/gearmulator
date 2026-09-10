// Instruction-level oracle for the reachable type-7 oscillator body in 101.
// The native function starts at the generated JMP P:$01fa target P:$0281;
// this runner supplies the same register/CCR boundary and executes the
// original words in the desktop DSP56300 interpreter.
#include "../core/nmm101_kernels.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace nmm::native;
using namespace dsp56k;

constexpr std::size_t kEntry = 0x281;
constexpr std::size_t kExit = 0x2cf;
constexpr std::array<Word24, kExit-kEntry> kProgram = {
    0x21e4c1,0xcd9b0b,0x20293e,0x203975,0x029008,0x21c741,
    0x20ee03,0x4fe400,0x203775,0x027008,0x21c5e4,0x46db51,
    0x203103,0x5ee400,0x202f40,0x027050,0x47db1c,0x218e4f,
    0x057418,0x205b2e,0x00feb9,0x018048,0x018048,0x018048,
    0x018048,0x018048,0x018048,0x018048,0x0c1dd3,0x200065,
    0x20293e,0x200040,0x000000,0x218e00,0x21e665,0x20297c,
    0x202178,0x5e5c00,0x21ef00,0x20ae58,0x5ee40a,0x050c1d,
    0x205b4c,0x20004f,0x059407,0x20ef40,0x200065,0x20293e,
    0x5c5c61,0x5ee418,0x050c14,0x20002e,0x00feb9,0x018048,
    0x018048,0x018048,0x018048,0x018048,0x018048,0x018048,
    0x0c1dd3,0x200065,0x20293e,0x200040,0x21e665,0x20ef00,
    0x20293e,0x5c5c00,0x20ae58,0x5ee40a,0x20003a,0x0c1c85,
    0x205b10,0x5f5c00,0x200009,0x0c1c85,0xf09b00,0x571000
};

struct ReferenceDsp
{
    DefaultMemoryValidator validator;
    PeripheralsNop peripheralX, peripheralY;
    Memory memory{validator,0x20000,0x200000,0x200000};
    DSP dsp{memory,&peripheralX,&peripheralY};
};

void require(bool condition, const char* message)
{
    if(!condition) throw std::runtime_error(message);
}

void compare(const GraphMemory& native, const ReferenceDsp& reference,
             const ModuleCursor& cursor, unsigned vector)
{
    for(std::size_t i=0;i<native.xWords;++i)
    {
        if(native.x[i]!=reference.memory.get(MemArea_X,static_cast<uint32_t>(i)))
        {
            std::cerr<<"x="<<std::hex<<native.x[i]<<" ref="<<reference.memory.get(MemArea_X,static_cast<uint32_t>(i))
                     <<" a="<<cursor.a<<" b="<<cursor.b<<" r3="<<cursor.r3<<" r4="<<cursor.r4
                     <<" dspA="<<(reference.dsp.regs().a.var>>8)<<" dspB="<<(reference.dsp.regs().b.var>>8)
                     <<" dspR3="<<reference.dsp.regs().r[3].var<<" dspR4="<<reference.dsp.regs().r[4].var<<'\n';
            throw std::runtime_error("oscillator X mismatch vector "+std::to_string(vector)+" address "+std::to_string(i));
        }
        if(native.y[i]!=reference.memory.get(MemArea_Y,static_cast<uint32_t>(i)))
            throw std::runtime_error("oscillator Y mismatch vector "+std::to_string(vector));
    }
    const auto& regs=reference.dsp.regs();
    require(regs.r[3].var==cursor.r3,"oscillator r3 mismatch");
    require(regs.r[4].var==cursor.r4,"oscillator r4 mismatch");
    require(regs.a.var==(static_cast<uint64_t>(cursor.a)<<8),"oscillator A mismatch");
    require(regs.b.var==(static_cast<uint64_t>(cursor.b)<<8),"oscillator B mismatch");
    require((regs.x.var&0xffffff)==cursor.x0,"oscillator X0 mismatch");
    require(((regs.x.var>>24)&0xffffff)==cursor.x1,"oscillator X1 mismatch");
    require((regs.y.var&0xffffff)==cursor.y0,"oscillator Y0 mismatch");
    require(((regs.y.var>>24)&0xffffff)==cursor.y1,"oscillator Y1 mismatch");
    require(!native.fault,"oscillator graph memory bounds fault");
}
}

int main(int argc, char** argv)
{
    try
    {
        const bool useJit = argc == 2 && std::string(argv[1]) == "--jit";
        require(argc == 1 || useJit,
                "usage: nmmOscillatorDifferential [--jit]");
        Logging::setLogFunc([](const std::string&){});
        constexpr unsigned vectors=4096;
        for(unsigned vector=0;vector<vectors;++vector)
        {
            ReferenceDsp reference;
            std::array<Word24,0x800> x{},y{};
            uint32_t seed=0x101007u^(vector*0x9e3779b9u);
            for(auto& word:x) { seed=seed*1664525u+1013904223u; word=seed&kMask24; }
            for(auto& word:y) { seed=seed*1664525u+1013904223u; word=seed&kMask24; }
            GraphMemory memory{x.data(),y.data(),x.size(),y.size(),false};
            ModuleCursor cursor;
            cursor.memory=&memory;
            cursor.r3=0x100+(vector&0x1f);
            cursor.r4=0x300+(vector&0x1f);
            cursor.r1=0x500;
            cursor.x0=(seed>>1)&kMask24;
            cursor.x1=(seed*3u)&kMask24;
            cursor.y0=(seed*5u)&kMask24;
            cursor.y1=(seed*7u)&kMask24;
            cursor.a=from24((seed*11u)&kMask24);
            cursor.b=from24((seed*13u)&kMask24);
            cursor.ccrC=(vector&1)!=0;
            cursor.ccrV=(vector&2)!=0;
            cursor.ccrZ=(vector&4)!=0;
            cursor.ccrN=(vector&8)!=0;

            for(std::size_t i=0;i<x.size();++i)
            {
                reference.memory.set(MemArea_X,static_cast<uint32_t>(i),x[i]);
                reference.memory.set(MemArea_Y,static_cast<uint32_t>(i),y[i]);
            }
            for(std::size_t i=0;i<kProgram.size();++i)
                reference.memory.set(MemArea_P,static_cast<uint32_t>(kEntry+i),kProgram[i]);
            reference.memory.set(MemArea_P,static_cast<uint32_t>(kExit),
                                 0x0c0000u | static_cast<uint32_t>(kExit));
            reference.dsp.getJit().notifyProgramMemWrite(static_cast<uint32_t>(kExit));
            auto config = reference.dsp.getJit().getConfig();
            config.maxInstructionsPerBlock = 16;
            config.dynamicFastInterrupts = true;
            config.linkJitBlocks = false;
            reference.dsp.getJit().setConfig(config);
            auto& regs=reference.dsp.regs();
            regs.r[3].var=cursor.r3;
            regs.r[4].var=cursor.r4;
            regs.r[1].var=cursor.r1;
            regs.a.var=static_cast<uint64_t>(cursor.a)<<8;
            regs.b.var=static_cast<uint64_t>(cursor.b)<<8;
            regs.x.var=cursor.x0|(static_cast<uint64_t>(cursor.x1)<<24);
            regs.y.var=cursor.y0|(static_cast<uint64_t>(cursor.y1)<<24);
            regs.sr.var=(cursor.ccrC?0x01:0)|(cursor.ccrV?0x02:0)
                       |(cursor.ccrZ?0x04:0)|(cursor.ccrN?0x08:0);
            reference.dsp.setPC(static_cast<uint32_t>(kEntry));

            const auto output=runOscillatorType7(cursor);
            unsigned instructions=0;
            while(reference.dsp.getPC().toWord()!=kExit)
            {
                require(instructions<kProgram.size(),"oscillator did not reach exit PC");
                if(useJit)
                    reference.dsp.execJit();
                else
                    reference.dsp.exec();
                ++instructions;
            }
            compare(memory,reference,cursor,vector);
            const auto referenceOutput=reference.memory.get(MemArea_X,0x10);
            require(output==referenceOutput,"oscillator output mismatch");
        }
        std::cout<<"PASS oscillator type7 native-vs-interpreter differential vectors="<<vectors<<'\n';
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL oscillator type7 differential: "<<error.what()<<'\n';
        return 1;
    }
    return 0;
}
