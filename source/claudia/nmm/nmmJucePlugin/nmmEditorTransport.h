#pragma once
#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace nmmJucePlugin
{
    // MIDI service thread <-> hardware worker only. Never used by process().
    struct EditorTransport
    {
        static constexpr size_t Capacity=65536;
        std::mutex mutex;
        std::deque<std::vector<uint8_t>> input;
        std::vector<uint8_t> output;
        size_t inputSize=0;
        uint64_t rejected=0;
        uint64_t received=0;
        std::atomic<uint64_t> revision{0};
        std::string ports;
        bool receive(const uint8_t* bytes,size_t size)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(size>Capacity-inputSize) {++rejected;return false;}
            ++received;
            input.emplace_back(bytes,bytes+size);inputSize+=size;
            if(size>=7 && bytes[0]==0xf0 && bytes[1]==0x33 && bytes[3]==6)
            {
                const auto command=bytes[2]&0x7c;
                const bool patchQuery=command==0x5c && (bytes[5]==2 || bytes[5]==20 || bytes[5]==53 || bytes[5]==32 || bytes[5]==75 || bytes[5]==83 || bytes[5]==76 || bytes[5]==102 || bytes[5]==99 || bytes[5]==97 || bytes[5]==78 || bytes[5]==104 || bytes[5]==112);
                if(command>=0x4c && !patchQuery) ++revision;
            }
            return true;
        }
    };
}
