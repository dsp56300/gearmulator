// Test the actual 101 oscillator pitch/setup sequence, including the
// previous-sample X:12 read before the new filter value is stored there.
#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_kernels.h"
#include "../mcu/nord101_mcu_contract.h"
#include "../mcu/nord101_reference_state.h"
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
constexpr std::array<uint32_t, 27> program{
    0x448f00,0x45db00,0x2400a0,0xf59b32,0x2400a2,0x47dbc2,0x0c1d04,0xf09b00,0x21ce00,0x0c1a8f,0x0c1c22,0x21f500,0x21d200,0x250000,0x2000e0,0x260040,0xd1aa32,0xc1a232,0x4de4c8,0xb09b32,0x21e700,0x2400c8,0x0c1d8f,0x47db00,0x5711ca,0x119bca,0x0c1c85};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint32_t next(uint32_t& seed) { seed=seed*1664525u+1013904223u; return seed>>8; }
}

int main(int argc,char** argv) {
    try {
        const bool useJit=argc==2 && std::string(argv[1])=="--jit";
        require(argc==1 || useJit,"usage: nmmOscillatorSetupDifferential [--jit]");
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
            for(const auto& w:mcu::k101ReadyX) x[w.address]=w.value;
            for(const auto& w:mcu::k101ReadyY) y[w.address]=w.value;
            for(const auto& w:mcu::k101SharedTableX) x[w.address]=w.value;
            for(const auto& w:mcu::k101SharedTableY) y[w.address]=w.value;
            x[0xf]=mcu::pitchWord(trial%128);
            for (unsigned i=0;i<x.size();++i) {
                memory.set(MemArea_X,i,x[i]); memory.set(MemArea_Y,i,y[i]);
            }
            ModuleCursor cursor;
            cursor.memory=&native; cursor.r3=0x60; cursor.r4=0x60; cursor.n2=0x7bc; cursor.n5=0x780;
            cursor.b=from24(next(seed));
            auto& regs=dsp.regs();
            regs.sr.var=0;
            regs.a.var=0;
            regs.b.var=static_cast<uint64_t>(cursor.b)<<8;
            regs.x.var=0;regs.y.var=0;
            for(unsigned i=0;i<8;++i) {
                regs.r[i].var=0;regs.n[i].var=0;regs.m[i].var=0xffffff;
            }
            regs.r[3].var=0x60;regs.r[4].var=0x60;regs.n[2].var=0x7bc;regs.n[5].var=0x780;
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
            runOscillatorType7Setup(cursor);
            require(!native.fault,"Native out-of-bounds access");
            for(unsigned i=0;i<x.size();++i) {
                if(x[i]!=memory.get(MemArea_X,i) || y[i]!=memory.get(MemArea_Y,i)) {
                    std::cerr<<"trial="<<trial<<" address="<<std::hex<<i<<'\n';
                    throw std::runtime_error("Setup memory mismatch");
                }
            }
            require((static_cast<uint64_t>(cursor.a)<<8)==static_cast<uint64_t>(regs.a.var),"Setup accumulator A mismatch");
            require((static_cast<uint64_t>(cursor.b)<<8)==static_cast<uint64_t>(regs.b.var),"Setup accumulator B mismatch");
            require(cursor.r1==regs.r[1].var && cursor.r3==regs.r[3].var && cursor.r4==regs.r[4].var,"Setup cursor mismatch");
            require(cursor.r2==regs.r[2].var && cursor.r5==regs.r[5].var,"Setup LUT cursor mismatch");
            require(cursor.x0==loword(regs.x).var && cursor.x1==hiword(regs.x).var && cursor.y0==loword(regs.y).var && cursor.y1==hiword(regs.y).var,"Setup data register mismatch");
        }
        std::cout<<"PASS 101 oscillator pitch/setup: 1024 differential vectors oracle="<<(useJit?"JIT":"interpreter")<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';return 1;
    }
}
