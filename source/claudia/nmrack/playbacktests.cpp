#include "playback.h"
#include "dsp56kBase/logging.h"
#include <iostream>
static void require(bool b,const char* s) {if(!b) throw std::runtime_error(s);}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3,"Usage: nmrackPlaybackTests ROM PATCH");Logging::setLogFunc([](const std::string&){});
        auto hw=std::make_unique<nmrack::Hardware>(argv[1],nmrack::Hardware::Options{false,true,true,true});
        hw->setStepBudget(450000000);hw->boot();
        auto patch=std::make_shared<const nmm::Patch>(nmm::Patch::load(argv[2]));
        nmrack::Playback playback(std::move(hw),48000,768,patch);playback.prefill(std::chrono::seconds(30));
        using C=nmrack::Command;
        require(playback.submit({C::Midi,19200,0x90,60,100,0}),"Queue note-on");
        require(playback.submit({C::Parameter,96000,1,4,0,0}),"Queue parameter mute");
        require(playback.submit({C::Parameter,192000,1,4,0,127}),"Queue parameter restore");
        require(playback.submit({C::Midi,288000,0x90,60,0,0}),"Queue zero-velocity note-off");
        std::array<nmrack::AudioFrame,128> audio;std::array<std::array<double,4>,4> sum{},squares{};std::array<unsigned,4> counts{};
        const auto start=std::chrono::steady_clock::now();unsigned onset=48000*4;
        nmrack::AudioFrame baseline{};
        for(unsigned done=0;done<48000*4;done+=128)
        {
            std::this_thread::sleep_until(start+std::chrono::nanoseconds(uint64_t(done+128)*1000000000/48000));
            playback.consume(audio.data(),128);playback.checkError();
            for(unsigned i=0;i<128;++i)
            {
                const auto frame=done+i;if(frame==4000) baseline=audio[i];
                if(frame>4000) for(unsigned ch=0;ch<4;++ch)
                    if(std::abs(audio[i][ch]-baseline[ch])>0.0002f) onset=std::min(onset,frame);
                // One late window in each second: note / muted / restored / released.
                const auto second=frame/48000;if(frame%48000<36000) continue;
                for(unsigned ch=0;ch<4;++ch) {const auto sample=audio[i][ch];sum[second][ch]+=sample;squares[second][ch]+=double(sample)*sample;}
                ++counts[second];
            }
        }
        playback.stop();playback.checkError();
        std::array<double,4> levels{};
        for(unsigned i=0;i<4;++i)
        {for(unsigned ch=0;ch<4;++ch) levels[i]+=std::max(0.,squares[i][ch]/counts[i]-std::pow(sum[i][ch]/counts[i],2));levels[i]=std::sqrt(levels[i]/4);}
        std::cout<<"host_onset_frame="<<onset<<" scheduled_frame=9600 onset_after_schedule_ms="<<(int(onset)-9600)/48.0
            <<" note="<<levels[0]<<" muted="<<levels[1]<<" restored="<<levels[2]<<" released="<<levels[3]
            <<" underruns="<<playback.underruns()<<" late="<<playback.lateCommands()<<'\n';
        require(onset>=9600&&onset<10560,"Timestamped MIDI onset includes bounded UART/resampler delay");
        require(levels[0]>.005&&levels[2]>.005,"Queued MIDI/parameter restore audible");
        // DAC has a channel-specific DC bias, so compare mute/release against each other.
        require(levels[1]<levels[0]*.1&&levels[3]<levels[0]*.1,"Queued mute and zero-velocity note-off silence output");
        require(!playback.underruns()&&!playback.rejectedCommands()&&!playback.lateCommands(),"Real-time worker controls meet deadlines");
        std::cout<<"PASS Rack timestamped worker controls\n";return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
