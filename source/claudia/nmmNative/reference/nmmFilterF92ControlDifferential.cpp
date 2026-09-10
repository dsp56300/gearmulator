// Differential for the 39 instruction words in the 101 Filter F/092
// control segment at P:$0175..$019a.  The extension word at P:$0187 is
// included in the program image but is consumed by P:$0186.
#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_filter_control.h"
#include "../mcu/nord101_mcu_contract.h"
#include "../mcu/nord101_reference_state.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"

#include <array>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>

namespace
{
using namespace dsp56k;
using namespace nmm::native;

constexpr std::array<uint32_t,38> kProgram={
    0xf49b00,0x458da0,0xf09be2,0x250000,0x2000e2,0x0c1d04,0x000000,
    0x21ce00,0x0c1a89,0x0c1c20,0x21f500,0x21d200,0x000000,0x000000,
    0xd1bb00,0x4fe500,0x4fdcc8,0x0a72c4,0x000780,0x21e600,0x6ddcd8,
    0x0c1d99,0x029078,0x02b078,0x20003e,0x000000,0x21e478,0x200081,
    0x000000,0x14dc00,0x2000a1,0x4edcea,0x21c500,0x44dbea,0x20004d,
    0x029048,0xf09b00,0x576500};

void require(bool value,const std::string& message)
{
    if(!value) throw std::runtime_error(message);
}

uint32_t next(uint32_t& state)
{
    state=state*1664525u+1013904223u;
    return (state>>8)&0xffffffu;
}

void compare(const GraphMemory& native,const DSP& dsp,const ModuleCursor& c)
{
    for(unsigned i=0;i<0x800;++i)
    {
        if(native.x[i]!=dsp.memory().get(MemArea_X,i))
            throw std::runtime_error("Filter control X mismatch at "+std::to_string(i)+" native="+std::to_string(native.x[i])+" dsp="+std::to_string(dsp.memory().get(MemArea_X,i)));
        if(native.y[i]!=dsp.memory().get(MemArea_Y,i))
            throw std::runtime_error("Filter control Y mismatch at "+std::to_string(i));
    }
    const auto& r=dsp.regs();
    if(native.fault)
        throw std::runtime_error("native Filter control bounds fault r2="+std::to_string(c.r2)+
            " r3="+std::to_string(c.r3)+" r4="+std::to_string(c.r4)+" r5="+std::to_string(c.r5));
    require(c.r2==static_cast<std::size_t>(r.r[2].var),"Filter control r2 mismatch");
    require(c.r3==static_cast<std::size_t>(r.r[3].var),"Filter control r3 mismatch");
    require(c.r4==static_cast<std::size_t>(r.r[4].var),"Filter control r4 mismatch");
    require(c.r5==static_cast<std::size_t>(r.r[5].var),"Filter control r5 mismatch");
    if(static_cast<int64_t>(static_cast<uint64_t>(c.a)<<8)!=r.a.var)
        throw std::runtime_error("Filter control A mismatch native="+std::to_string(c.a)+" dsp="+std::to_string(r.a.var>>8));
    if(static_cast<int64_t>(static_cast<uint64_t>(c.b)<<8)!=r.b.var)
        throw std::runtime_error("Filter control B mismatch native="+std::to_string(c.b)+" dsp="+std::to_string(r.b.var>>8));
    require(c.x0==static_cast<uint32_t>(loword(r.x).var),"Filter control X0 mismatch");
    require(c.x1==static_cast<uint32_t>(hiword(r.x).var),"Filter control X1 mismatch");
    require(c.y0==static_cast<uint32_t>(loword(r.y).var),"Filter control Y0 mismatch");
    require(c.y1==static_cast<uint32_t>(hiword(r.y).var),"Filter control Y1 mismatch");
}
}

int main(int argc,char** argv)
{
    try
    {
        const bool useJit=argc==2 && std::string(argv[1])=="--jit";
        require(argc==1 || useJit,"usage: nmmFilterF92ControlDifferential [--jit]");
        Logging::setLogFunc([](const std::string&){});
        constexpr unsigned trials=10000;
        DefaultMemoryValidator validator;
        PeripheralsNop px,py;
        Memory memory(validator,0x20000,0x200000,0x200000);
        DSP dsp(memory,&px,&py);
        // Relocate the straight-line fragment to P:$0000 for the JIT oracle;
        // none of its data addresses depend on the original graph PC.
        for(unsigned i=0;i<kProgram.size();++i) memory.set(MemArea_P,i,kProgram[i]);
        // Keep a legal one-word terminator in the JIT's program cache; the
        // differential loop stops at this PC and never executes it.
        memory.set(MemArea_P,kProgram.size(),0x0c0000|kProgram.size());
        dsp.getJit().notifyProgramMemWrite(kProgram.size());
        auto config=dsp.getJit().getConfig();
        // A boundary oracle must not let one JIT block run into the envelope
        // fragment following P:$019a.
        config.maxInstructionsPerBlock=1;
        config.dynamicFastInterrupts=true;
        config.linkJitBlocks=false;
        dsp.getJit().setConfig(config);

        std::array<Word24,0x800> x{},y{};
        uint32_t state=0x101092u;
        for(unsigned trial=0;trial<trials;++trial)
        {
            for(unsigned i=0;i<x.size();++i) { x[i]=next(state); y[i]=next(state); }
            // Keep all AGU-derived pointers in the 0x800-word test window.
            // x0 is the preceding oscillator/control result and zero makes
            // the generated r2/r5 lookup pointers deterministic at zero;
            // later coefficient and clamp values remain randomized.
            const std::size_t r3=trial<512?0x73:0x120;
            const std::size_t r4=trial<512?0x6a:0x180;
            x[0x0d]=0x000100;
            if(trial<512)
            {
                for(const auto& word:mcu::k101ReadyX) x[word.address]=word.value;
                for(const auto& word:mcu::k101ReadyY) y[word.address]=word.value;
                for(const auto& word:mcu::k101SharedTableX) x[word.address]=word.value;
                for(const auto& word:mcu::k101SharedTableY) y[word.address]=word.value;
                constexpr uint8_t velocities[]{1,64,100,127};
                x[0x0d]=mcu::velocityWordForMidi(velocities[trial/128]);
            }
            else
            {
                x[r3]=0x000100;
                y[r4]=0x000100;
                y[r4+3]=0;
            }
            for(unsigned i=0;i<x.size();++i) { memory.set(MemArea_X,i,x[i]); memory.set(MemArea_Y,i,y[i]); }
            memory.set(MemArea_X,0x0d,x[0x0d]);

            auto& r=dsp.regs();
            r.sr.var=0; r.a.var=0; r.b.var=0; r.x.var=0; r.y.var=0;
            for(unsigned i=0;i<8;++i) { r.r[i].var=0; r.n[i].var=0; r.m[i].var=0xffffff; }
            r.r[3].var=r3; r.r[4].var=r4; r.n[5].var=0x780;
            r.x.var=trial<512?mcu::pitchWord(trial%128):((trial&1)?0x000100:0);
            dsp.setPC(0);
#ifdef NMM_FILTER_CONTROL_TRACE
            if(trial==0) std::fprintf(stderr,"set pc=%x\n",dsp.getPC().toWord());
#endif

            GraphMemory native{x.data(),y.data(),x.size(),y.size(),false};
            ModuleCursor cursor; cursor.memory=&native; cursor.r3=r3; cursor.r4=r4;
            cursor.x0=static_cast<Word24>(r.x.var&0xffffff);
            cursor.n5=0x780;
            cursor.a=0; cursor.b=0;
            if(useJit) dsp.getJit().checkModeChange();
            unsigned instructions=0;
            for(; instructions<45 && dsp.getPC().toWord()!=kProgram.size(); ++instructions)
            {
                if(useJit) dsp.execJit(); else dsp.exec();
#ifdef NMM_FILTER_CONTROL_TRACE
                if(trial==4817) { const auto& q=dsp.regs(); std::fprintf(stderr,"dsp %03x a=%lld b=%lld x0=%06x x1=%06x y0=%06x y1=%06x r2=%x r3=%x r4=%x r5=%x\n",dsp.getPC().toWord()-1,(long long)(q.a.var>>8),(long long)(q.b.var>>8),loword(q.x).var,hiword(q.x).var,loword(q.y).var,hiword(q.y).var,q.r[2].var,q.r[3].var,q.r[4].var,q.r[5].var); }
#endif
            }
            if(!useJit && instructions!=37)
                throw std::runtime_error("trial="+std::to_string(trial)+" Filter control instruction count mismatch pc="+std::to_string(dsp.getPC().toWord())+" count="+std::to_string(instructions));
            runFilterF92Control(cursor);
            try { compare(native,dsp,cursor); }
            catch(const std::exception& error) {
                throw std::runtime_error("trial="+std::to_string(trial)+" "+error.what());
            }
        }
        std::cout<<"PASS 101 Filter F/092 control: 38 words (37 instructions), "<<trials
                 <<" differential vectors oracle="<<(useJit?"JIT":"interpreter")<<"\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL 101 Filter F/092 control differential: "<<error.what()<<'\n';
        return 1;
    }
}
