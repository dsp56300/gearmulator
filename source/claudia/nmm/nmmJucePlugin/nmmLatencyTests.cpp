#include "nmmDevice.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc,char** argv)
{
    try
    {
        if(argc!=3 && argc!=4) throw std::runtime_error("usage: nmmLatencyTests firmware StereoInput.pch [audio-driven-frames]");
        std::ifstream f(argv[2]);if(!f) throw std::runtime_error("Missing fixture");
        const std::string text{std::istreambuf_iterator<char>(f),{}};
        for(const unsigned block:{64u,256u,512u,2230u}) for(const unsigned extra:{0u,384u})
        {
            auto panel=std::make_shared<nmmJucePlugin::PanelState>();panel->patchText=text;panel->offline=true;if(argc==4) {panel->audioDrivenFrames=std::stoul(argv[3]);panel->audioDrivenLinkedJit=panel->audioDrivenFrames!=0;}
            nmmJucePlugin::Device device({},argv[1],panel);device.setProcessingBlockSize(block);device.setExtraLatencySamples(extra);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(panel->loading && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(!panel->ready) {std::lock_guard<std::mutex> lock(panel->mutex);throw std::runtime_error(panel->status);}
            std::vector<float> input(block),left(block),right(block);synthLib::TAudioInputs in{};in[0]=input.data();
            synthLib::TAudioOutputs out{};out[0]=left.data();out[1]=right.data();std::vector<synthLib::SMidiEvent> midi,reply;
            const unsigned reported=device.getInternalLatencyInputToOutput()+extra;
            unsigned first=~0u;const unsigned warmup=((24000+block-1)/block)*block;
            for(unsigned offset=0;offset<warmup+reported+2*block+32;offset+=block)
            {
                // Span the native input sampling phase; a one-frame pulse can fall
                // between graph input reads in the current digital ADC bridge.
                std::fill(input.begin(),input.end(),0.0f);
                if(offset==warmup) std::fill_n(input.begin(),8,0.5f);
                device.process(in,out,block,midi,reply);
                for(unsigned i=0;i<block;++i)
                {
                    if(offset>=warmup && std::abs(left[i])>0.000001f && first==~0u) first=offset+i;
                    if(offset>=warmup && std::abs(right[i])>0.000001f) throw std::runtime_error("Input pulse leaked to other input channel");
                }
            }
            std::cout<<"block="<<block<<" extra="<<extra<<" reported_input_latency="<<reported<<" first_audio="<<(first-warmup)<<'\n';
            // Reported delay excludes only the small native converter/graph phase.
            if(first==~0u || first<warmup+reported || first>warmup+reported+16) throw std::runtime_error("Measured audio delay disagrees with reported latency");
            if(panel->underruns || panel->droppedJobs) throw std::runtime_error("Offline latency test lost frames");
        }
        std::cout<<"PASS input pulse latency, host block sizing and additional framework delay\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
