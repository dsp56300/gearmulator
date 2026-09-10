#pragma once
#include "audioqueue.h"
#include "libresample/include/libresample.h"
#include <cmath>
#include <memory>

namespace nmrack
{
    // Worker-only band-limited conversion. Persistent state and unconsumed input
    // survive every host block; no resampling work belongs to the device callback.
    class RateAdapter
    {
        struct Close {void operator()(void* p) const {resample_close(p);}};
        std::array<std::unique_ptr<void,Close>,4> filters;
        std::array<std::array<float,128>,4> input{},output{};
        unsigned offset=0,available=0;
        double ratio;
    public:
        explicit RateAdapter(double rate):ratio(rate/96000.0)
        {
            if(!std::isfinite(rate)||rate<32000||rate>192000) throw std::invalid_argument("Output rate must be 32000..192000 Hz");
            if(rate!=96000) for(auto& filter:filters)
            {filter.reset(resample_open(1,ratio,ratio));if(!filter) throw std::runtime_error("Cannot create output resampler");}
        }
        template<class Pull> void render(AudioFrame* destination,unsigned count,Pull&& pull)
        {
            if(ratio==1) {pull(destination,count);return;}
            unsigned stalled=0;
            while(count)
            {
                if(!available)
                {
                    std::array<AudioFrame,128> source;
                    pull(source.data(),128);
                    for(unsigned i=0;i<128;++i) for(unsigned ch=0;ch<4;++ch) input[ch][i]=source[i][ch];
                    offset=0;available=128;
                }
                int consumed=-1,produced=-1;
                for(unsigned ch=0;ch<4;++ch)
                {
                    int used=0;
                    const auto n=resample_process(filters[ch].get(),ratio,input[ch].data()+offset,int(available),0,&used,
                        output[ch].data(),int(std::min(count,128u)));
                    if(n<0||used<0||unsigned(used)>available) throw std::runtime_error("Output resampler failed");
                    if(ch&&(consumed!=used||produced!=n)) throw std::runtime_error("Resampler channel synchronization lost");
                    consumed=used;produced=n;
                }
                if(!consumed&&!produced) {if(++stalled>16) throw std::runtime_error("Output resampler stalled");}else stalled=0;
                for(int i=0;i<produced;++i) for(unsigned ch=0;ch<4;++ch) destination[i][ch]=output[ch][i];
                offset+=unsigned(consumed);available-=unsigned(consumed);destination+=produced;count-=unsigned(produced);
            }
        }
    };
}
