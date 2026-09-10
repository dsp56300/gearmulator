#pragma once
#include "audioqueue.h"

namespace nmrack
{
    enum class Monitor { Pair12,Pair34,Mix,Four };
    inline float monitorSample(const AudioFrame& frame,unsigned channel,Monitor monitor,float gain) noexcept
    {
        const auto sample=monitor==Monitor::Mix?(frame[channel]+frame[channel+2])*0.5f
            :frame[channel+(monitor==Monitor::Pair34?2:0)];
        return sample*gain;
    }
}
