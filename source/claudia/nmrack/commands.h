#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <mutex>

namespace nmrack
{
    struct Command
    {
        enum Kind {Midi,Parameter};
        Kind kind=Midi;
        uint64_t frame=0; // native 96-kHz rendered timeline, relative to Playback start
        uint16_t a=0,b=0,c=0,d=0;
    };
    // Bounded multi-producer control inbox. Never used by the AUDIO callback.
    // Producers serialize short fixed-array edits. Worker uses try_lock and
    // continues rendering if a producer is descheduled while holding the lock.
    class Commands
    {
    public:
        static constexpr unsigned Capacity=256;
        bool clear()
        {
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock) return false;
            size=0;return true;
        }
        bool push(Command command)
        {
            std::lock_guard lock(mutex);
            if(size==Capacity) return false;
            unsigned i=size++;
            while(i&&items[i-1].frame>command.frame) {items[i]=items[i-1];--i;}
            items[i]=command;return true;
        }
        // Returns a due command, or shortens the render grant to its timestamp.
        // Preserve FIFO order at equal timestamps; throttle MIDI to wire speed.
        bool next(uint64_t now,uint64_t midiReady,Command& result,unsigned& grant)
        {
            std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock||!size) return false;
            const auto due=items[0].kind==Command::Midi?std::max(items[0].frame,midiReady):items[0].frame;
            if(due>now) {grant=unsigned(std::min(uint64_t(grant),due-now));return false;}
            result=items[0];--size;
            for(unsigned i=0;i<size;++i) items[i]=items[i+1];
            return true;
        }
    private:
        std::mutex mutex;
        std::array<Command,Capacity> items{};
        unsigned size=0;
    };
}
