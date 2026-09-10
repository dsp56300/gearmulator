#include "hardware.h"
#include "dsp56kBase/logging.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

static void require(bool condition,const char* message)
{if(!condition) throw std::runtime_error(message);}

static std::vector<nmrack::AudioFrame> render(const std::string& rom,bool linked,unsigned fixed,unsigned loadDelay=0)
{
    nmrack::Hardware hw(rom,{false,linked,true,true,true});hw.setStepBudget(450000000);
    hw.boot();hw.advance(loadDelay);hw.initializeOutputTones();hw.advance(96000);
    std::array<nmrack::AudioFrame,1024> warm{};hw.renderAudio(warm.data(),unsigned(warm.size()));
    std::vector<nmrack::AudioFrame> result(9600);
    const auto start=std::chrono::steady_clock::now();
    const auto before=hw.status();unsigned index=0;
    const unsigned chunks[]{1,7,63,64,127,128,255};
    for(unsigned done=0;done<result.size();)
    {
        const auto count=std::min(unsigned(result.size())-done,fixed?fixed:chunks[index++%7]);
        hw.renderAudio(result.data()+done,count);done+=count;
    }
    const auto after=hw.status();
    require(after.dacFrames>=result.size()+warm.size()&&after.dacFrames<result.size()+warm.size()+64,"Exact DAC target with bounded surplus");
    require(after.dacWordsChecked>=4*(after.dacFrames-before.dacFrames),"Fresh serial payloads checked against DMA sources");
    require(after.dacMaxSkew<1,"Output pair skew below one sample");
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"DAC worker linked="<<linked<<" target="<<fixed<<" load_delay="<<loadDelay<<" realtime_factor="<<(.1/elapsed)
        <<" frames="<<after.dacFrames<<" checked_words="<<after.dacWordsChecked<<" held_words="<<after.dacHeldWords<<'\n';
    return result;
}

static void checkTones(const std::vector<nmrack::AudioFrame>& audio)
{
        constexpr double pi=3.14159265358979323846;
        const double expected[]{220,277.1826309768721,349.2282314330039,440};
        bool valid=true;
        for(unsigned channel=0;channel<4;++channel)
        {
            double mean=0;for(const auto& f:audio) {require(std::isfinite(f[channel]),"Nonfinite DAC output");mean+=f[channel];}mean/=audio.size();
            double square=0;std::array<double,4> magnitude{};
            for(unsigned tone=0;tone<4;++tone)
            {
                double re=0,im=0;
                for(unsigned i=0;i<audio.size();++i)
                {
                    const double sample=audio[i][channel]-mean;
                    const double window=.5-.5*std::cos(2*pi*i/(audio.size()-1));
                    re+=sample*window*std::cos(2*pi*expected[tone]*i/96000);
                    im+=sample*window*std::sin(2*pi*expected[tone]*i/96000);
                    if(!tone) square+=sample*sample;
                }
                magnitude[tone]=4*std::hypot(re,im)/audio.size();
            }
            double other=0;for(unsigned i=0;i<4;++i) if(i!=channel) other=std::max(other,magnitude[i]);
            const auto rms=std::sqrt(square/audio.size());
            const bool correct=rms>.005&&rms<.1&&magnitude[channel]>.007&&magnitude[channel]>10*other;
            valid &= correct;
            std::cout<<"output="<<channel+1<<" expected_hz="<<expected[channel]<<" ac_rms="<<rms<<" tone_amplitudes=";
            for(auto amplitude:magnitude) std::cout<<amplitude<<',';
            std::cout<<(correct?" PASS":" FAIL")<<'\n';
        }
        require(valid,"Four-output acceptance failed; do not promote transaction DAC reconstruction to validated playback");
}

int main(int argc,char** argv)
{
    try
    {
        require(argc==2,"Usage: nmrackDacTests firmware");Logging::setLogFunc([](const std::string&){});
        const auto audio=render(argv[1],true,0);
        require(audio==render(argv[1],false,128),"DAC linked/unlinked output differs");
        require(audio==render(argv[1],true,7),"DAC output depends on buffer size");
        std::cout<<"PASS serial transaction checks and bit-identical buffer/JIT comparisons\n";
        checkTones(audio);
        for(auto delay:{1u,17u}) checkTones(render(argv[1],true,128,delay));
        std::cout<<"PASS four independent output tones (physical analog latch wiring still unmeasured)\n";
        return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
