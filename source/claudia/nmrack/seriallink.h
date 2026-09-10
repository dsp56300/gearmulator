#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace nmrack
{
    // Diagnostic frame-level wire. Timestamps are machine frames, not wall time.
    // A complete two-word packet introduces framing latency; this is not yet a
    // pin-accurate substitute for asynchronous ESSI word/FS edges.
    class SerialLink
    {
    public:
        struct Packet {double time;std::array<uint32_t,2> words;};
        static constexpr unsigned Capacity=32;
        void push(Packet packet,bool receiverEnabled)
        {
            if(packet.time<lastTime) throw std::runtime_error("Serial timestamp moved backwards");
            lastTime=packet.time;
            if(!receiverEnabled) {clear();++disabled;return;}
            if(write-read==Capacity) throw std::runtime_error("Serial link overflow");
            packets[write++%Capacity]=packet;
            highWater=std::max(highWater,unsigned(write-read));
        }
        bool pop(double now,Packet& packet)
        {
            if(read==write||packets[read%Capacity].time>now) {++missing;return false;}
            packet=packets[read++%Capacity];++delivered;return true;
        }
        void clear() {read=write;}
        uint64_t delivered=0,missing=0,disabled=0;
        unsigned highWater=0;
    private:
        std::array<Packet,Capacity> packets{};
        uint64_t read=0,write=0;
        double lastTime=0;
    };
}
