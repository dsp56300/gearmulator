#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_envelope.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dsp56k;
using namespace nmm::native;
// Raw ADSR control fragment with the three actual 101 compiler substitutions:
// zero unconnected input, gate X:0e, gain #$20 (short immediate), output X:13.
constexpr std::array<uint32_t,32> program{
    0xf09b00,0xf78300,0x270000,0x2000f4,0x475b71,0x203103,0x027048,0x56e300,
    0x568e03,0x02f048,0x565b03,0x02f058,0x000000,0x21f200,0x21e500,0x4ed400,
    0x63e300,0x44da00,0x47da41,0x2000c6,0x44da32,0x224fb2,0x44dbd2,0x025068,
    0x189b00,0x5f5cd2,0x242000,0x145c00,0x2000a0,0x0c1d04,0x000000,0x561300};
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
uint32_t next(uint32_t& seed) {seed=seed*1664525u+1013904223u;return seed>>8;}
}

int main(int argc,char** argv) {
    try {
        const bool useJit=argc==2 && std::string(argv[1])=="--jit";
        require(argc==1 || useJit,"usage: nmmEnvelopeDifferential [--jit]");
        Logging::setLogFunc([](const std::string&){});
        DefaultMemoryValidator validator;
        PeripheralsNop px,py;
        Memory memory(validator,0x20000,0x200000,0x200000);
        DSP dsp(memory,&px,&py);
        for(unsigned i=0;i<program.size();++i) memory.set(MemArea_P,i,program[i]);
        memory.set(MemArea_P,program.size(),0x0c0000|program.size());
        dsp.getJit().notifyProgramMemWrite(program.size());
        auto config=dsp.getJit().getConfig();config.maxInstructionsPerBlock=16;
        config.dynamicFastInterrupts=true;config.linkJitBlocks=false;
        dsp.getJit().setConfig(config);
        std::array<Word24,0x800> x{},y{};
        uint32_t seed=0x101020;
        for(unsigned trial=0;trial<10000;++trial) {
            GraphMemory native{x.data(),y.data(),x.size(),y.size(),false};
            for(unsigned i=0;i<x.size();++i) {x[i]=next(seed);y[i]=next(seed);}
            x[0x120]=0x200;y[0x180]=0x240;y[0x181]=0x220;x[0x123]=0x300;
            x[0x122]=(trial&1)?0x200000:0;
            x[0x0e]=(trial&2)?0x200000:0;
            for(unsigned i=0;i<x.size();++i) {memory.set(MemArea_X,i,x[i]);memory.set(MemArea_Y,i,y[i]);}
            ModuleCursor cursor;cursor.memory=&native;cursor.r3=0x120;cursor.r4=0x180;
            auto& regs=dsp.regs();
            regs.sr.var=0;regs.a.var=0;regs.b.var=0;regs.x.var=0;regs.y.var=0;
            for(unsigned i=0;i<8;++i) {regs.r[i].var=0;regs.n[i].var=0;regs.m[i].var=0xffffff;}
            regs.r[3].var=0x120;regs.r[4].var=0x180;
            dsp.setPC(0);
            if(useJit) dsp.getJit().checkModeChange();
            for(unsigned i=0;i<128 && dsp.getPC().toWord()!=program.size();++i) {
                if(useJit) dsp.execJit();
                else dsp.exec();
            }
            if(dsp.getPC().toWord()!=program.size()) {
                std::cerr<<"trial="<<trial<<" pc="<<std::hex<<dsp.getPC().toWord()<<'\n';
                throw std::runtime_error("Envelope stop PC mismatch");
            }
            runEnvelope20Control(cursor);
            require(!native.fault,"Envelope native bounds fault");
            for(unsigned i=0;i<x.size();++i) {
                if(x[i]!=memory.get(MemArea_X,i) || y[i]!=memory.get(MemArea_Y,i)) {
                    std::cerr<<"trial="<<trial<<" address="<<std::hex<<i<<" X native="<<x[i]<<" dsp="<<memory.get(MemArea_X,i)<<" Y native="<<y[i]<<" dsp="<<memory.get(MemArea_Y,i)<<'\n';
                    throw std::runtime_error("Envelope memory mismatch");
                }
            }
            require((static_cast<uint64_t>(cursor.a)<<8)==static_cast<uint64_t>(regs.a.var),"Envelope A mismatch");
            require((static_cast<uint64_t>(cursor.b)<<8)==static_cast<uint64_t>(regs.b.var),"Envelope B mismatch");
            require(cursor.r2==regs.r[2].var && cursor.r3==regs.r[3].var && cursor.r4==regs.r[4].var,"Envelope cursor mismatch");
            require(cursor.x0==loword(regs.x).var && cursor.x1==hiword(regs.x).var && cursor.y0==loword(regs.y).var && cursor.y1==hiword(regs.y).var,"Envelope data register mismatch");
        }
        std::cout<<"PASS 101-specialized ADSR control: 10000 differential vectors oracle="<<(useJit?"JIT":"interpreter")<<'\n';
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
