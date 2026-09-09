#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace nmm
{
    // Board-specific SCC2691 connection. All access belongs to the emulator
    // worker. The external queue models a MIDI cable, not a larger UART FIFO.
    class PcPort
    {
    public:
        bool receive(const uint8_t* bytes, size_t count)
        {
            if(count>input.size()-(inputWrite-inputRead)) return false;
            for(size_t i=0;i<count;++i) input[inputWrite++%input.size()]=bytes[i];
            return true;
        }
        void advance(uint32_t cycles, uint32_t clock)
        {
            byteCycles=(clock+3124)/3125;
            elapsedCycles+=cycles;
            if(txRemaining>cycles) txRemaining-=cycles;
            else if(txRemaining) {txRemaining=0;if(output.size()<65536) output.push_back(txByte);}
            if(inputRead==inputWrite) {rxCredit=0;return;}
            rxCredit+=uint64_t(cycles)*3125;
            while(rxCredit>=clock && inputRead!=inputWrite)
            {
                rxCredit-=clock;
                const auto b=input[inputRead++%input.size()];
                lastReceiveCycle=elapsedCycles;
                if(rxEnabled)
                {
                    if(rxWrite-rxRead<3) fifo[rxWrite++%3]=b;
                    else overrun=true;
                }
            }
        }
        bool interrupt() const {return rxWrite!=rxRead && rxEnabled;}
        bool idle(uint32_t clock) const {return inputIdle(clock) && !txRemaining && elapsedCycles-lastTransmitCycle>clock/20;}
        // Read-only snapshots need settled incoming edits, not silence from
        // unsolicited outgoing meter/knob notifications.
        bool inputIdle(uint32_t clock) const {return inputRead==inputWrite && rxRead==rxWrite && elapsedCycles-lastReceiveCycle>clock/20;}
        bool takeInterrupt() {if(!interrupt() || interruptSent) return false;interruptSent=true;return true;}
        void bus(uint8_t control, uint8_t data)
        {
            const auto reg=((control>>3)&1)|((control>>5)&6);
            if(!(control&1))
            {
                if((previous&4) && !(control&4)) write(reg,data);
                if((previous&2) && !(control&2)) latched=read(reg);
            }
            previous=control;
        }
        uint8_t data() const {return latched;}
        std::vector<uint8_t> takeOutput() {std::vector<uint8_t> result;result.swap(output);return result;}
    private:
        uint8_t read(unsigned reg)
        {
            if(reg==0) {auto v=mode[modeIndex];modeIndex=1;return v;}
            if(reg==1) return (rxRead!=rxWrite?1:0)|(rxWrite-rxRead==3?2:0)|(txEnabled&&!txRemaining?12:0)|(overrun?16:0);
            if(reg==3) {interruptSent=false;return rxRead!=rxWrite?fifo[rxRead++%3]:0;}
            if(reg==5) return (interrupt()?2:0)|(txEnabled&&!txRemaining?1:0);
            return 0;
        }
        void write(unsigned reg,uint8_t value)
        {
            if(reg==0) {mode[modeIndex]=value;modeIndex=1;}
            if(reg==2)
            {
                switch(value>>4)
                {
                    case 1: modeIndex=0;break;
                    case 2: rxRead=rxWrite=0;rxEnabled=false;overrun=false;interruptSent=false;break;
                    case 3: txRemaining=0;txEnabled=false;break;
                    case 4: overrun=false;break;
                }
                if(value&1) rxEnabled=true;
                if(value&2) rxEnabled=false;
                if(value&4) txEnabled=true;
                if(value&8) txEnabled=false;
            }
            if(reg==3 && txEnabled && !txRemaining) {txByte=value;txRemaining=byteCycles;lastTransmitCycle=elapsedCycles;}
        }
        std::array<uint8_t,65536> input{};
        uint64_t inputRead=0,inputWrite=0,rxCredit=0,rxRead=0,rxWrite=0;
        // One increment per MCU step; RX/TX timestamps change only on traffic.
        uint64_t elapsedCycles=0,lastReceiveCycle=0,lastTransmitCycle=0;
        std::array<uint8_t,3> fifo{};
        std::array<uint8_t,2> mode{};
        std::vector<uint8_t> output;
        uint32_t txRemaining=0,byteCycles=6711;
        uint8_t previous=7,latched=0,txByte=0,modeIndex=0;
        bool rxEnabled=false,txEnabled=false,overrun=false,interruptSent=false;
    };
}
