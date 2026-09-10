// Differential test for the resident master/DAC prefix P:$1d1..$1d8.
#define DSP56K_FORCE_INTERPRETER

#include "../core/nmm101_dac.h"
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
using namespace dsp56k;
using namespace nmm::native;

constexpr std::array<uint32_t,8> kResident{
    0x4edc00, // move y:(r4)+,y0
    0xf9e300, // move x:(r3),a y:(r7)+,y1
    0xfdfbb2, // mac +y1,y0,a x:(r3)+,b y:(r7)+,y1
    0x4fdfba, // mac +y1,y0,b y:(r7)+,y1
    0x4fdfb2, // mac +y1,y0,a y:(r7)+,y1
    0x2000ba, // mac +y1,y0,b
    0x5e5900, // move a,y:(r1)+
    0x5f5900  // move b,y:(r1)+
};

void require(bool condition,const std::string& message)
{
    if(!condition) throw std::runtime_error(message);
}

uint32_t next(uint32_t& seed)
{
    seed=seed*1664525u+1013904223u;
    return seed>>8;
}

void checkDecoderBoundaries()
{
    constexpr std::array<uint32_t,8> words{
        0,1,0x1ffff,0x20000,0x20001,0x3ffff,0x40000,0xffffff};
    constexpr std::array<int32_t,8> expected{
        0,1,131071,-131072,-131071,-1,0,-1};
    for(unsigned i=0;i<words.size();++i)
        require(decodeDacSigned18(words[i])==expected[i],"DAC decoder boundary mismatch");
}
}

int main(int argc,char** argv)
{
    try
    {
        const bool useJit=argc==2 && std::string(argv[1])=="--jit";
        require(argc==1 || useJit,"usage: nmmDacDifferential [--jit]");
        checkDecoderBoundaries();

        Logging::setLogFunc([](const std::string&){});
        DefaultMemoryValidator validator;
        PeripheralsNop px,py;
        Memory memory(validator,0x20000,0x200000,0x200000);
        DSP dsp(memory,&px,&py);
        for(unsigned i=0;i<kResident.size();++i) memory.set(MemArea_P,i,kResident[i]);
        memory.set(MemArea_P,kResident.size(),0x0c0000|kResident.size());
        dsp.getJit().notifyProgramMemWrite(kResident.size());
        auto config=dsp.getJit().getConfig();
        config.maxInstructionsPerBlock=16;
        config.dynamicFastInterrupts=true;
        config.linkJitBlocks=false;
        dsp.getJit().setConfig(config);

        std::array<Word24,4> input{};
        uint32_t seed=0xdac101;
        constexpr unsigned trials=10000;
        for(unsigned trial=0;trial<trials;++trial)
        {
            const auto master=trial==0?Word24(0x00d297):mask24(next(seed));
            const auto offset=trial==0?Word24(0x155):mask24(next(seed));
            for(auto& word:input) word=mask24(next(seed));
            if(trial==0) input.fill(0); // captured master=100 idle output
            if(trial==1) input={0x7fffff,0x800000,0xffffff,0x000001};

            const auto expected=runResidentDac(input.data(),master,offset);
            memory.set(MemArea_X,0x5f,offset);
            memory.set(MemArea_Y,0x5f,master);
            for(unsigned i=0;i<input.size();++i) memory.set(MemArea_Y,0x300+i,input[i]);

            auto& regs=dsp.regs();
            regs.sr.var=0;
            regs.a.var=0;regs.b.var=0;regs.x.var=0;regs.y.var=0;
            for(unsigned i=0;i<8;++i)
            {
                regs.r[i].var=0;regs.n[i].var=0;regs.m[i].var=0xffffff;
            }
            regs.r[1].var=0x300;
            regs.r[3].var=0x5f;
            regs.r[4].var=0x5f;
            regs.r[7].var=0x300;
            dsp.setPC(0);
            if(useJit) dsp.getJit().checkModeChange();
            unsigned calls=0;
            while(dsp.getPC().toWord()!=kResident.size() && calls++<128)
            {
                if(useJit) dsp.execJit(); else dsp.exec();
            }
            require(dsp.getPC().toWord()==kResident.size(),"Resident sequence stopped at unexpected PC");
            const auto actualLeft=memory.get(MemArea_Y,0x300);
            const auto actualRight=memory.get(MemArea_Y,0x301);
            if(actualLeft!=expected.leftWord || actualRight!=expected.rightWord)
            {
                std::cerr<<"trial="<<trial<<" expected="<<std::hex<<expected.leftWord<<','<<expected.rightWord
                         <<" actual="<<actualLeft<<','<<actualRight<<'\n';
                throw std::runtime_error("Resident word mismatch");
            }
            require(decodeDacSigned18(actualLeft)==expected.leftSigned18 &&
                    decodeDacSigned18(actualRight)==expected.rightSigned18,
                    "Resident DAC decode mismatch");
        }

        const std::array<Word24,4> idle{0,0,0,0};
        const auto master100=runResidentDac(idle.data(),0x00d297,0x155);
        require(master100.leftSigned18==341 && master100.rightSigned18==341,
                "Master-100 idle output is not the captured 341 word");
        std::cout<<"PASS resident master/DAC differential: "<<trials
                 <<" vectors oracle="<<(useJit?"JIT":"interpreter")<<"\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL "<<error.what()<<'\n';
        return 1;
    }
}
