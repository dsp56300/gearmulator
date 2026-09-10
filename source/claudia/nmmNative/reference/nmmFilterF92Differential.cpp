// Exact module differential: generated 092 sample template versus the first
// native Filter F kernel.  This intentionally runs one isolated DSP sample
// with controlled X/Y state so a mismatch identifies arithmetic or parallel
// move ordering, rather than MCU scheduling or codec transport.

// Keep the reference on the DSP interpreter.  The production JIT is validated
// separately; this runner is the instruction-level oracle for the kernel.
#define DSP56K_FORCE_INTERPRETER
#include "../core/nmm101_kernels.h"
#include "filter_f92_oracle.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
using dsp56k::EMemArea;
using dsp56k::MemArea_P;
using dsp56k::MemArea_X;
using dsp56k::MemArea_Y;
using nmm::native::GraphMemory;
using nmm::native::ModuleCursor;
using nmm::native::Word24;
using nmm::native::mpy;
using nmm::native::mac;
using nmm::native::sub;
using nmm::native::asl;
using nmm::native::from24;
using nmm::native::to24;
using nmm::native::round24;
using nmm::native::mask24;
using nmm::native::limit24;

constexpr std::array<Word24,34> kFilterProgram={
    0xF09B00,0x5680D8,0x0C1D87,0x200014,0x000000,0x21C600,
    0x4FDC98,0x2000B0,0x21E400,0x45DBD8,0x000000,0x21E400,
    0x2000A7,0xF19B00,0x21C6C2,0x45DBD6,0x4ED400,0x5E5CF2,
    0x000000,0x21C7D2,0x2000C6,0x000000,0xB69BE2,0x478000,
    0x21C6A2,0x45D3D6,0x20CF00,0x181BA2,0x2000D6,0x20CF00,
    0x565B09,0x03F4AE,0x000000,0x570000};

void require(bool condition,const std::string& message)
{
    if(!condition) throw std::runtime_error(message);
}

std::string word(Word24 value)
{
    std::ostringstream out;
    out<<"0x"<<std::hex<<std::setw(6)<<std::setfill('0')<<(value&0xffffff);
    return out.str();
}

#ifdef NMM_FILTER_TRACE
void traceNative(ModuleCursor& c,std::size_t input)
{
    auto& m=*c.memory;
    auto dump=[&](const char* pc){std::cerr<<"native pc="<<pc<<" a="<<std::hex<<c.a<<" b="<<c.b<<" r3="<<c.r3<<" r4="<<c.r4<<" x0="<<c.x0<<" x1="<<c.x1<<" y0="<<c.y0<<" y1="<<c.y1<<"\n";};
    c.x0=c.xReadInc();c.y0=c.yReadInc();dump("0");
    c.b=mpy(c.y0,c.x0);c.a=from24(m.readX(input));dump("1");
    c.b=asl(c.b,3);dump("2");
    c.a=sub(c.a,c.b);dump("3");
    c.y0=limit24(c.a);dump("5");
    c.y1=c.yReadInc();c.b=mpy(c.y0,c.y0);dump("6");
    c.a=mpy(c.y1,c.y0);dump("7");
    c.x0=limit24(c.b);c.x1=c.xReadInc();dump("8/9");
    c.b=mpy(c.y0,c.x0);c.x0=limit24(c.b);dump("b");
    c.a=round24(sub(c.a,mpy(c.x1,c.x0)));dump("c");
    c.x0=c.xReadInc();c.y1=c.yReadInc();dump("d");
    c.a=mac(c.a,c.x0,c.y1);c.x1=c.xReadInc();dump("e/f");
    c.a=sub(c.a,mpy(c.y0,c.x0));c.y0=c.yReadDec();dump("10");
    c.a=mac(c.a,c.y1,c.x1);c.yWriteInc(to24(c.a));dump("11");
    c.a=mac(c.a,c.y0,c.x0);c.y1=limit24(c.a);dump("13");
    c.a=sub(c.a,mpy(c.x0,c.y1));c.x1=c.xReadInc();dump("14/15");
    c.a=mac(c.a,c.x1,c.y0);c.yWriteInc(limit24(c.a));dump("16");
    c.y1=mask24(m.readX(input));c.a=mac(c.a,c.x1,c.x0);c.y0=limit24(c.a);dump("17/18");
    c.x1=c.xReadDec();c.b=from24(c.y0);c.a=mac(c.a,c.x1,c.x0);c.y0=limit24(c.a);dump("19");
    c.a=sub(c.a,mpy(c.y0,c.x0));c.b=from24(c.y0);c.b=c.a;c.xWriteInc(limit24(c.a));m.writeY(c.r4-4,limit24(c.a));m.writeX(input,limit24(c.b));dump("1c-21");
}
#endif

struct ReferenceDsp
{
    dsp56k::DefaultMemoryValidator validator;
    dsp56k::PeripheralsNop peripheralX,peripheralY;
    dsp56k::Memory memory{validator,0x20000,0x200000,0x200000};
    dsp56k::DSP dsp{memory,&peripheralX,&peripheralY};

    ReferenceDsp()
    {
        for(unsigned i=0;i<kFilterProgram.size();++i) memory.set(MemArea_P,i,kFilterProgram[i]);
    }
};

void compareState(const GraphMemory& native,const ReferenceDsp& reference,const ModuleCursor& cursor)
{
    for(std::size_t i=0;i<0x800;++i)
    {
        const auto x=reference.memory.get(MemArea_X,static_cast<uint32_t>(i));
        const auto y=reference.memory.get(MemArea_Y,static_cast<uint32_t>(i));
        if(native.x[i]!=x) throw std::runtime_error("X mismatch at "+std::to_string(i)+" native="+word(native.x[i])+" dsp="+word(x));
        if(native.y[i]!=y) throw std::runtime_error("Y mismatch at "+std::to_string(i)+" native="+word(native.y[i])+" dsp="+word(y));
    }
    require(!native.fault,"native graph memory bounds fault");
    const auto& regs=reference.dsp.regs();
    require(regs.r[3].var==cursor.r3,"r3 mismatch");
    require(regs.r[4].var==cursor.r4,"r4 mismatch");
    require(regs.a.var==(static_cast<int64_t>(cursor.a)<<8),"A mismatch");
    require(regs.b.var==(static_cast<int64_t>(cursor.b)<<8),"B mismatch");
    require(dsp56k::loword(regs.x).var==static_cast<int32_t>(cursor.x0),"X0 mismatch");
    require(dsp56k::hiword(regs.x).var==static_cast<int32_t>(cursor.x1),"X1 mismatch");
    require(dsp56k::loword(regs.y).var==static_cast<int32_t>(cursor.y0),"Y0 mismatch");
    require(dsp56k::hiword(regs.y).var==static_cast<int32_t>(cursor.y1),"Y1 mismatch");
}
}

struct BoardVector
{
    uint32_t index = 0;
    uint32_t seed = 0;
    uint16_t r3 = 0;
    uint16_t r4 = 0;
    uint32_t output = 0;
    uint64_t hash = 0;
};

void writeBoardFixture(const char* path,const std::array<BoardVector,256>& vectors,
                       std::size_t count)
{
    std::ofstream out(path);
    if(!out) throw std::runtime_error(std::string("cannot write board fixture: ")+path);
    out<<"#pragma once\n\n"
       <<"// Generated by nmmFilterF92Differential --emit-board.\n"
       <<"// Expected values come from the DSP56300 interpreter, not the native kernel.\n"
       <<"#include <cstddef>\n#include <cstdint>\n\n"
       <<"namespace nmm::native::filter_f92_fixture\n{\n"
       <<"struct Vector { uint32_t index, seed; uint16_t r3, r4; uint32_t output; uint64_t hash; };\n"
       <<"constexpr Vector kVectors[] = {\n";
    for(std::size_t i=0;i<count;++i)
    {
        const auto& v=vectors[i];
        out<<"    {"<<std::dec<<v.index<<"u, 0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<v.seed
           <<std::dec<<"u, 0x"<<std::hex<<std::setw(4)<<std::setfill('0')<<v.r3
           <<", 0x"<<std::setw(4)<<v.r4<<", 0x"<<std::setw(6)<<v.output
           <<", 0x"<<std::setw(16)<<v.hash<<"ull},\n";
    }
    out<<"};\nconstexpr std::size_t kVectorCount = sizeof(kVectors)/sizeof(kVectors[0]);\n}\n";
    if(!out) throw std::runtime_error(std::string("failed writing board fixture: ")+path);
}

int main(int argc,char** argv)
{
    try
    {
        // The interpreter emits JIT diagnostics while constructing its cache;
        // this runner is an offline numeric oracle, so retain only failures.
        Logging::setLogFunc([](const std::string&){});
        const char* boardPath=nullptr;
        if(argc==3 && std::string(argv[1])=="--emit-board") boardPath=argv[2];
        else if(argc!=1) throw std::runtime_error("usage: nmmFilterF92Differential [--emit-board PATH]");
        constexpr unsigned vectors=10000;
        constexpr unsigned boardVectors=256;
        std::array<BoardVector,boardVectors> board{};
        for(unsigned vector=0;vector<vectors;++vector)
        {
            ReferenceDsp reference;
            std::array<Word24,0x800> nativeX{},nativeY{};
            GraphMemory nativeMemory{nativeX.data(),nativeY.data(),nativeX.size(),nativeY.size(),false};
            const uint32_t seed=0x101f92u^(vector*0x9e3779b9u);
            nmm::native::filter_f92_oracle::initialize(vector,seed,nativeMemory.x,nativeMemory.y);
            for(std::size_t i=0;i<nmm::native::filter_f92_oracle::kWords;++i)
            {
                reference.memory.set(MemArea_X,static_cast<uint32_t>(i),nativeMemory.x[i]);
                reference.memory.set(MemArea_Y,static_cast<uint32_t>(i),nativeMemory.y[i]);
            }
            // Vary the allocated cursor so every AGU-relative state offset is
            // exercised while keeping the input and output outside the graph
            // coefficient cells.
            const std::size_t r3=0x80+(vector%0x100),r4=0x180+(vector%0x100);
            reference.dsp.regs().r[3].var=static_cast<int32_t>(r3);
            reference.dsp.regs().r[4].var=static_cast<int32_t>(r4);
            reference.dsp.setPC(0);

            ModuleCursor cursor;
            cursor.memory=&nativeMemory;cursor.r3=r3;cursor.r4=r4;
#ifdef NMM_FILTER_TRACE
            if(vector==0) {auto traceCursor=cursor;traceNative(traceCursor,0);}
#endif
            const auto nativeOutput=nmm::native::runFilterF92(cursor,0);

            // There are no extension words in this template. The final MOVE
            // at P:$21 advances to P:$22, giving an exact stop condition.
            unsigned instructions=0;
            while(reference.dsp.getPC().toWord()!=kFilterProgram.size())
            {
                require(instructions<64,"DSP template did not reach its stop PC");
                reference.dsp.exec();
                ++instructions;
#ifdef NMM_FILTER_TRACE
                if(vector==0)
                {
                    const auto& trace=reference.dsp.regs();
                    std::cerr<<"pc="<<std::hex<<instructions-1<<" a="<<trace.a.var<<" b="<<trace.b.var
                             <<" x="<<trace.x.var<<" y="<<trace.y.var<<" r3="<<trace.r[3].var
                             <<" r4="<<trace.r[4].var<<" x0="<<dsp56k::loword(trace.x).var
                             <<" x1="<<dsp56k::hiword(trace.x).var<<" y0="<<dsp56k::loword(trace.y).var
                             <<" y1="<<dsp56k::hiword(trace.y).var<<"\n";
                }
#endif
            }
            require(instructions==kFilterProgram.size(),"Unexpected DSP instruction count");
            compareState(nativeMemory,reference,cursor);
            const auto dspOutput=reference.memory.get(MemArea_X,0);
            require(nativeOutput==dspOutput,"Filter output mismatch vector="+std::to_string(vector)+" native="+word(nativeOutput)+" dsp="+word(dspOutput));

            if(vector<boardVectors)
            {
                std::array<Word24,nmm::native::filter_f92_oracle::kWords> dspX{},dspY{};
                for(std::size_t i=0;i<nmm::native::filter_f92_oracle::kWords;++i)
                {
                    dspX[i]=reference.memory.get(MemArea_X,static_cast<uint32_t>(i));
                    dspY[i]=reference.memory.get(MemArea_Y,static_cast<uint32_t>(i));
                }
                const auto& regs=reference.dsp.regs();
                board[vector]={vector,seed,static_cast<uint16_t>(r3),static_cast<uint16_t>(r4),
                    dspOutput,nmm::native::filter_f92_oracle::hashState(dspX.data(),dspY.data(),
                        static_cast<uint32_t>(regs.r[3].var),static_cast<uint32_t>(regs.r[4].var),
                        static_cast<int64_t>(regs.a.var>>8),static_cast<int64_t>(regs.b.var>>8),
                        static_cast<uint32_t>(dsp56k::loword(regs.x).var),
                        static_cast<uint32_t>(dsp56k::hiword(regs.x).var),
                        static_cast<uint32_t>(dsp56k::loword(regs.y).var),
                        static_cast<uint32_t>(dsp56k::hiword(regs.y).var))};
            }
        }
        if(boardPath) writeBoardFixture(boardPath,board,board.size());
        std::cout<<"PASS Filter F/092 native-vs-interpreter differential instructions="
                 <<kFilterProgram.size()<<" vectors="<<vectors;
        if(boardPath) std::cout<<" board_vectors="<<board.size()<<" path="<<boardPath;
        std::cout<<'\n';
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL Filter F/092 differential: "<<error.what()<<'\n';
        return 1;
    }
}
