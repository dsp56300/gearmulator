#include "synthLib/resamplerInOut.h"
#include <array>
#include <iostream>
#include <stdexcept>
int main()
{
    try {
        for(unsigned rate:{44100u,48000u,96000u}) for(unsigned block:{1u,32u,64u,128u,256u})
        {
            synthLib::ResamplerInOut resampler(2,2);resampler.setSamplerates(rate,96000);
            std::array<float,256> left{},right{};
            synthLib::TAudioInputs input{};input[0]=left.data();input[1]=right.data();
            synthLib::TAudioOutputs output{};output[0]=left.data();output[1]=right.data();
            std::vector<synthLib::SMidiEvent> midi,response;unsigned received=0;
            for(unsigned i=0;i<2100;++i)
            {
                midi.clear();
                if(i<2048) midi.emplace_back(synthLib::MidiEventSource::Host,0x90,i&127,i>>7,block-1);
                resampler.process(input,output,midi,response,block,[&](const auto&,const auto& out,size_t size,const auto& events,auto&) {
                    for(auto& event:events) {
                        if((unsigned(event.b)|(unsigned(event.c)<<7))!=received++) throw std::runtime_error("MIDI lost/reordered at rate="+std::to_string(rate)+" block="+std::to_string(block));
                        if(event.offset>=size) throw std::runtime_error("Invalid MIDI offset");
                    }
                    for(unsigned ch=0;ch<2;++ch) std::fill_n(out[ch],size,0.f);
                });
            }
            if(received!=2048) throw std::runtime_error("Pending MIDI lost");
            std::cout<<"PASS MIDI rate="<<rate<<" block="<<block<<'\n';
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
