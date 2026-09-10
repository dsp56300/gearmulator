#pragma once
#include "audioqueue.h"

namespace nmrack
{
    // Firmware-transaction DAC reconstruction, not a claim about physical latch
    // wiring. Only newly loaded TX words enter here; held/undriven slots do not.
    // DMA provenance validates channel/bank order without sourcing audio from RAM.
    class DacOutput
    {
    public:
        static float decode(uint32_t word) noexcept
        {return float(int32_t((word&0x3ffff)^0x20000)-0x20000)/131072.0f;}
        bool word(unsigned port,uint32_t address,uint32_t value,double time,AudioFrame& result)
        {
            const auto bank=address&~0x1fu,channel=address&0x1fu;
            if(port>1||(bank!=0x6c0&&bank!=0x6e0)||channel>=4||channel/2!=port)
                throw std::runtime_error("DAC TX DMA source is outside its output pair");
            if(!synchronized)
            {
                synchronized=true;currentBank=bank;mask=0;start=time;
            }
            if(bank!=currentBank)
            {
                if(settled) throw std::runtime_error("DAC bank changed with an incomplete or repeated output frame");
                startupDiscarded+=unsigned(mask!=0);mask=0;
                currentBank=bank;start=time;
            }
            if(!mask) start=time;
            if(mask&(1u<<channel)) throw std::runtime_error("DAC repeated a fresh output channel");
            if(time<start||time-start>1.0) throw std::runtime_error("DAC output pair exceeded one sample period");
            // The native four-output module writes inputs [2,1,4,3] to
            // consecutive mix locations. Expose logical jack order [1,2,3,4].
            // This mapping is firmware-derived, not measured analog wiring.
            frame[channel^1]=decode(value);mask|=1u<<channel;++words;
            if(mask!=15) return false;
            if(settled&&(time-lastFrameTime<.8||time-lastFrameTime>1.2))
                throw std::runtime_error("DAC output cadence is not one frame per sample");
            result=frame;mask=0;++frames;settled=true;lastFrameTime=time;
            maxSkew=std::max(maxSkew,time-start);
            // Same-bank repetition is illegal even after a completed frame.
            currentBank=bank^0x20;start=time;
            return true;
        }
        void resetCapture() {synchronized=false;settled=false;mask=0;}
        uint64_t words=0,frames=0,startupDiscarded=0;
        double maxSkew=0;
    private:
        AudioFrame frame{};
        uint32_t currentBank=0,mask=0;
        bool synchronized=false,settled=false;
        double start=0,lastFrameTime=0;
    };
}
