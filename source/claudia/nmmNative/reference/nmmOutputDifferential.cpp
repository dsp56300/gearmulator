// Test the actual compiled 101 amplifier/output sequence, including the
// previous-sample X:12 read before the new filter value is stored there.
#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_kernels.h"
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
constexpr std::array<uint32_t, 17> program{
    0x449200,0x571200,0x469300,0x2000d0,0x0c1d04,0x618400,
    0x561400,0x79dc00,0x469400,0x479400,0x44db00,0x5ec900,
    0x5ed900,0x5fd1d2,0x2000ca,0x5e5900,0x5f5900};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint32_t next(uint32_t& seed) { seed=seed*1664525u+1013904223u; return seed>>8; }
}

int main(int argc,char** argv) {
    try {
        const bool useJit=argc==2 && std::string(argv[1])=="--jit";
        require(argc==1 || useJit,"usage: nmmOutputDifferential [--jit]");
        Logging::setLogFunc([](const std::string&){});
        DefaultMemoryValidator validator;
        PeripheralsNop px,py;
        Memory memory(validator,0x20000,0x200000,0x200000);
        DSP dsp(memory,&px,&py);
        for (unsigned i=0;i<program.size();++i) memory.set(MemArea_P,i,program[i]);
        memory.set(MemArea_P,program.size(),0x0c0000|program.size());
        dsp.getJit().notifyProgramMemWrite(program.size());
        auto config=dsp.getJit().getConfig();config.maxInstructionsPerBlock=16;
        config.dynamicFastInterrupts=true;config.linkJitBlocks=false;
        dsp.getJit().setConfig(config);
        std::array<Word24,0x800> x{},y{};
        uint32_t seed=0x101004;
        for (unsigned trial=0;trial<1024;++trial) {
            GraphMemory native{x.data(),y.data(),x.size(),y.size(),false};
            for (unsigned i=0;i<x.size();++i) {x[i]=next(seed); y[i]=next(seed);}
            x[4]=0x300;
            // Positive, zero and negative offsets exercise post-update order.
            const int offset=int(trial%5)-2;
            y[0x180]=uint32_t(offset)&0xffffff;
            for (unsigned i=0;i<x.size();++i) {
                memory.set(MemArea_X,i,x[i]); memory.set(MemArea_Y,i,y[i]);
            }
            ModuleCursor cursor;
            cursor.memory=&native; cursor.r3=0x120; cursor.r4=0x180;
            cursor.b=from24(next(seed));
            auto& regs=dsp.regs();
            regs.sr.var=0;
            regs.a.var=0;
            regs.b.var=static_cast<uint64_t>(cursor.b)<<8;
            regs.x.var=0;regs.y.var=0;
            for(unsigned i=0;i<8;++i) {
                regs.r[i].var=0;regs.n[i].var=0;regs.m[i].var=0xffffff;
            }
            regs.r[3].var=0x120;regs.r[4].var=0x180;
            dsp.setPC(0);
            if(useJit) dsp.getJit().checkModeChange();
            for(unsigned i=0;i<128 && dsp.getPC().toWord()!=program.size();++i) {
                if(useJit) dsp.execJit();
                else dsp.exec();
            }
            if(dsp.getPC().toWord()!=program.size()) {
                std::cerr<<"trial="<<trial<<" pc="<<std::hex<<dsp.getPC().toWord()<<'\n';
                throw std::runtime_error("Unexpected stop PC");
            }
            const auto result=runOutput4(cursor);
            require(!native.fault,"Native out-of-bounds access");
            for(unsigned i=0;i<x.size();++i) {
                if(x[i]!=memory.get(MemArea_X,i) || y[i]!=memory.get(MemArea_Y,i)) {
                    std::cerr<<"trial="<<trial<<" address="<<std::hex<<i<<'\n';
                    throw std::runtime_error("Output memory mismatch");
                }
            }
            require((static_cast<uint64_t>(cursor.a)<<8)==static_cast<uint64_t>(regs.a.var),"Output accumulator A mismatch");
            require((static_cast<uint64_t>(cursor.b)<<8)==static_cast<uint64_t>(regs.b.var),"Output accumulator B mismatch");
            require(cursor.r1==regs.r[1].var && cursor.r3==regs.r[3].var && cursor.r4==regs.r[4].var,"Output cursor mismatch");
            require((uint32_t(cursor.n1)&0xffffff)==regs.n[1].var,"Output offset mismatch");
            require(result.left==limit24(cursor.a) && result.right==limit24(cursor.b),"Output return mismatch");
        }
        std::cout<<"PASS compiled 101 amplifier/output: 1024 differential vectors oracle="<<(useJit?"JIT":"interpreter")<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
