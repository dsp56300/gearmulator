#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace nmrack
{
    using AudioFrame=std::array<float,4>;
    class AudioQueue
    {
    public:
        static constexpr unsigned Capacity=1024;
        void push(const AudioFrame& frame)
        {
            if(size()==Capacity) throw std::runtime_error("nmrack audio queue overflow");
            frames[write++%Capacity]=frame;
            highWater=std::max(highWater,size());
        }
        unsigned pop(AudioFrame* out,unsigned count)
        {
            count=std::min(count,size());
            for(unsigned i=0;i<count;++i) out[i]=frames[read++%Capacity];
            return count;
        }
        unsigned size() const {return unsigned(write-read);}
        void clear() {read=write;}
        unsigned highWater=0;
    private:
        std::array<AudioFrame,Capacity> frames{};
        uint64_t read=0,write=0;
    };
}
