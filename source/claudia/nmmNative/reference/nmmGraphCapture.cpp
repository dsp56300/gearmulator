// Per-host-frame 101 graph/DAC mapping probe.
//
// This uses only the public Hardware diagnostic API. MCU-led execution is
// intentional: render(1) drains one output frame and then exposes the DSP
// words at the resulting frame boundary. No emulator production code or
// scheduling behavior is changed.

#include "../../nmm/nmmLib/nmmcapture.h"
#include "../../nmm/nmmLib/nmmhardware.h"
#include "../../nmm/nmmLib/nmmrom.h"
#include "../../nmm/nmmLib/nmmpatch.h"

#include <cstdint>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Path=std::string;

void require(bool ok,const std::string& message)
{
    if(!ok) throw std::runtime_error(message);
}

uint32_t bits(float value)
{
    uint32_t result=0;
    std::memcpy(&result,&value,sizeof(result));
    return result;
}

int32_t signed18(float value)
{
    const auto decoded=static_cast<int64_t>(std::llround(double(value)*131072.0));
    require(decoded>=-131072 && decoded<=131071,"DAC word outside signed 18-bit range");
    require(std::abs(double(value)-double(decoded)/131072.0)==0.0,
            "DAC sample is not an exact signed-18 decode");
    return static_cast<int32_t>(decoded);
}

struct Words
{
    uint32_t x0d,x0e,x0f,x04,x05,x10,x12,x13,x14;
    uint32_t y6c0,y6c1,y6e0,y6e1,ySelected0,ySelected1;
};

Words readWords(const nmm::Hardware& hardware)
{
    auto x=[&](uint32_t address){return hardware.readMemory('X',address);};
    auto y=[&](uint32_t address){return hardware.readMemory('Y',address);};
    const auto selector=x(0x05);
    return {x(0x0d),x(0x0e),x(0x0f),x(0x04),selector,x(0x10),x(0x12),x(0x13),x(0x14),
            y(0x6c0),y(0x6c1),y(0x6e0),y(0x6e1),y(selector),y(selector+1)};
}

void writeHeader(std::ofstream& output)
{
    output<<"host_frame,phase,dsp_frame_after,dsp_cycles,control_counter,"
             "x0d,x0e,x0f,x04,x05,x10,x12,x13,x14,y6c0,y6c1,y6e0,y6e1,"
             "y_selected0,y_selected1,"
             "left_int18,right_int18,left_float_bits,right_float_bits\n";
}

void capturePhase(nmm::Hardware& hardware,std::ofstream& output,
                  const char* phase,uint64_t& hostFrame,unsigned count,
                  uint64_t budget,std::vector<std::array<int32_t,2>>* audioCapture)
{
    for(unsigned i=0;i<count;++i)
    {
        const auto audio=hardware.render(1,budget);
        require(audio.size()==1,"Hardware returned an unexpected frame count");
        const auto timing=hardware.timing();
        const auto words=readWords(hardware);
        require(timing.frames>0,"Hardware frame counter did not advance");
        const auto left=signed18(audio[0][0]);
        const auto right=signed18(audio[0][1]);
        if(audioCapture) audioCapture->push_back({left,right});
        // host_frame is the exact ordinal of the returned sample in this
        // capture. timing().frames is the DSP-produced frame counter after
        // render; it can stay unchanged while render drains an already queued
        // ESSI sample, so it is deliberately recorded as a separate field.
        output<<hostFrame++<<','<<phase<<','<<timing.frames<<','<<timing.cycles<<','
              <<timing.controlSampleCounter<<','
              <<std::hex<<std::setfill('0')
              <<std::setw(6)<<words.x0d<<','<<std::setw(6)<<words.x0e<<','
              <<std::setw(6)<<words.x0f<<','<<std::setw(6)<<words.x04<<','
              <<std::setw(6)<<words.x05<<','
              <<std::setw(6)<<words.x10<<','<<std::setw(6)<<words.x12<<','
              <<std::setw(6)<<words.x13<<','<<std::setw(6)<<words.x14<<','
              <<std::setw(6)<<words.y6c0<<','<<std::setw(6)<<words.y6c1<<','
              <<std::setw(6)<<words.y6e0<<','<<std::setw(6)<<words.y6e1<<','
              <<std::setw(6)<<words.ySelected0<<','<<std::setw(6)<<words.ySelected1<<','
              <<std::dec<<left<<','<<right<<','
              <<"0x"<<std::hex<<std::setw(8)<<bits(audio[0][0])<<','
              <<"0x"<<std::setw(8)<<bits(audio[0][1])<<std::dec<<'\n';
    }
}
}

int main(int argc,char** argv)
{
    try
    {
        require(argc>=3 && argc<=8,
                "usage: nmmGraphCapture firmware.bin output.csv [101.pch] [warmup] [frames] [dma.csv] [audio.int18]");
        const Path firmware=argv[1];
        const Path outputPath=argv[2];
        const Path patch=argc>=4?Path(argv[3]):Path("nord-micro-modular/patches/101.pch");
        const unsigned warmup=argc>=5?static_cast<unsigned>(std::stoul(argv[4])):256;
        const unsigned frames=argc>=6?static_cast<unsigned>(std::stoul(argv[5])):4096;
        const Path dmaPath=argc>=7?Path(argv[6]):Path();
        const Path audioPath=argc>=8?Path(argv[7]):Path();
        require(warmup<=65536 && frames>0 && frames<=65536,"warmup/frames outside 0..65536");

        const auto fixture=nmm::Patch::load(patch);
        nmm::Hardware hardware(firmware);
        hardware.boot(30000000);
        hardware.loadPatch(fixture,30000000);
        hardware.setMasterVolume(100);
        if(!dmaPath.empty())
        {
            // Boundary tracing is deliberately opt-in. The public diagnostic
            // API records before/after peripheral polls and therefore runs the
            // DSP interpreter; this is the exact bus-boundary view, while the
            // normal graph CSV remains on the ordinary MCU-led/JIT path.
            // The OS visits several DSP/JIT blocks per sample. 1024 entries
            // per requested output frame keeps short diagnostic windows whole;
            // the API remains bounded at one million observations and reports
            // truncation in its first CSV line for longer runs.
            const auto requested=(uint64_t(warmup)+frames+256u)*1024u;
            const auto capacity=static_cast<size_t>(std::min<uint64_t>(1048576u,
                std::max<uint64_t>(32768u,requested)));
            hardware.enableDmaBoundaryTrace(capacity);
        }

        std::ofstream output(outputPath);
        require(bool(output),"Cannot write "+outputPath);
        output<<"# scheduler=mcu-led-render-one-frame\n"
              <<"# x0d=velocity x0e=gate x0f=pitch x04=output-pointer x05=DMA-mix-selector "
                 "x10=filter-input x12=filter-output x13=envelope x14=amplifier\n"
              <<"# Y:6c0/6c1 and Y:6e0/6e1 are the two observed stereo mix pairs\n"
              <<"# state_timing=post-render boundary; host_frame=returned-sample ordinal; "
                 "dsp_frame_after=timing.frames (queued ESSI samples may share it); "
                 "next resident mix target=Y[X:05],Y[X:05+1]\n"
              <<"# resident P:1d1..1d8: Y[X:05+k] * Y:5f plus X:5f, k=0..3; graph writes X:04\n";
        if(!dmaPath.empty()) output<<"# dma_trace="<<dmaPath<<" (optional public diagnostic boundary trace)\n";
        writeHeader(output);

        uint64_t hostFrame=0;
        std::vector<std::array<int32_t,2>> audioCapture;
        if(!audioPath.empty()) audioCapture.reserve(uint64_t(warmup)+frames+256u);
        auto* audioOut=audioPath.empty()?nullptr:&audioCapture;
        capturePhase(hardware,output,"warmup",hostFrame,warmup,10000000,audioOut);
        hardware.sendMidi(0x90,60,100);
        capturePhase(hardware,output,"attack",hostFrame,frames,10000000,audioOut);
        hardware.sendMidi(0x80,60,0);
        capturePhase(hardware,output,"release",hostFrame,256,10000000,audioOut);
        require(bool(output),"Short graph capture write");
        if(!dmaPath.empty())
        {
            std::ofstream dma(dmaPath);
            require(bool(dma),"Cannot write "+dmaPath);
            hardware.writeDmaBoundaryTrace(dma);
            require(bool(dma),"Short DMA boundary trace write");
        }
        if(!audioPath.empty())
        {
            std::ofstream audio(audioPath,std::ios::binary);
            require(bool(audio),"Cannot write "+audioPath);
            const char magic[4]={'N','M','M','I'};
            const uint32_t version=1,count=static_cast<uint32_t>(audioCapture.size());
            audio.write(magic,sizeof magic);
            audio.write(reinterpret_cast<const char*>(&version),sizeof version);
            audio.write(reinterpret_cast<const char*>(&count),sizeof count);
            for(const auto& frame:audioCapture)
                audio.write(reinterpret_cast<const char*>(frame.data()),sizeof(int32_t)*2);
            require(bool(audio),"Short audio capture write");
        }

        std::cout<<"PASS nmmGraphCapture output="<<outputPath
                 <<" frames="<<hostFrame<<"\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"FAIL nmmGraphCapture: "<<error.what()<<'\n';
        return 1;
    }
}
