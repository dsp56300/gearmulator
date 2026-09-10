#include "hardware.h"
#include "dsp56kBase/logging.h"
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
static void require(bool b,const char* s) {if(!b) throw std::runtime_error(s);}
int main(int argc,char** argv)
{
    try
    {
        require(argc==3||argc==4,"Usage: nmrackMidiTests ROM PATCH [requested voices]");Logging::setLogFunc([](const std::string&){});
        nmrack::Hardware hw(argv[1],{false,true,true,true});hw.setStepBudget(450000000);hw.boot();
        auto patch=nmm::Patch::load(argv[2]);patch.controllers.push_back({1,4,0,12});
        if(argc==4) {const auto n=std::stoi(argv[3]);require(n>=4&&n<=32,"Test voices must be 4..32");patch.requestedVoices=uint8_t(n);}
        hw.loadPatch(patch);hw.advance(96000);hw.setStepBudget(std::numeric_limits<uint64_t>::max());
        const auto allocation=hw.voices();require(allocation.allocated>=4&&allocation.allocated<=patch.requestedVoices,"Bounded voice allocation");
        std::cout<<"allocated="<<allocation.allocated;
        for(unsigned i=0;i<allocation.allocated;++i) std::cout<<" voice"<<i<<"_dsp="<<unsigned(allocation.voice[i].dsp)<<"_program="<<allocation.voice[i].program;
        std::cout<<std::endl;
        std::set<std::pair<unsigned,unsigned>> programs;std::set<unsigned> dspIds;
        for(unsigned i=0;i<allocation.allocated;++i)
        {const auto& v=allocation.voice[i];require(v.dsp<4,"Voice DSP within Rack board");programs.insert({v.dsp,v.program});dspIds.insert(v.dsp);}
        require(programs.size()==allocation.allocated,"Independent DSP program per voice");
        std::cout<<"voice_dsp_count="<<dspIds.size()<<std::endl;
        nmrack::AudioFrame last{};
        auto rms=[&](unsigned frames)
        {
            std::array<nmrack::AudioFrame,128> block;std::array<double,4> sums{},squares{};
            for(unsigned done=0;done<frames;)
            {
                const auto n=std::min(128u,frames-done);hw.renderAudio(block.data(),n);done+=n;
                last=block[n-1];for(unsigned i=0;i<n;++i) for(unsigned ch=0;ch<4;++ch)
                {const auto s=block[i][ch];require(std::isfinite(s),"Finite MIDI output");sums[ch]+=s;squares[ch]+=s*s;}
            }
            double e=0;for(unsigned ch=0;ch<4;++ch) e+=std::max(0.,squares[ch]/frames-std::pow(sums[ch]/frames,2));
            return std::sqrt(e/4);
        };
        const auto silent=rms(9600);
        const auto baseline=last;hw.sendMidi(0x90,60,100);
        unsigned onset=9600;std::array<nmrack::AudioFrame,128> onsetBlock;
        for(unsigned done=0;done<9600;done+=128)
        {
            hw.renderAudio(onsetBlock.data(),128);
            for(unsigned i=0;i<128;++i) for(unsigned ch=0;ch<4;++ch)
                if(std::abs(onsetBlock[i][ch]-baseline[ch])>0.0002f) onset=std::min(onset,done+i);
        }
        require(onset<9600,"MIDI reaches DAC within bounded time");
        std::cout<<"native_midi_onset_frames="<<onset<<" native_midi_onset_ms="<<onset/96.0<<std::endl;
        const auto note=rms(9600);
        std::cout<<"silence="<<silent<<" note="<<note<<std::endl;
        require(note>silent*10&&note>0.001,"MIDI note-on produces audio");
        hw.sendMidi(0xe0,127,127);rms(12000);
        std::vector<nmrack::AudioFrame> bent(9600);hw.renderAudio(bent.data(),unsigned(bent.size()));
        double mean=0;for(const auto& f:bent) mean+=f[0];mean/=bent.size();unsigned crossings=0;
        for(unsigned i=1;i<bent.size();++i) if(bent[i-1][0]<mean&&bent[i][0]>=mean) ++crossings;
        std::cout<<"bent_pitch_crossings_100ms="<<crossings<<std::endl;
        require(crossings>=28&&crossings<=30,"Native two-semitone pitch bend");
        hw.sendMidi(0xe0,0,64);rms(12000);
        hw.sendMidi(0x80,60,0);rms(96000);const auto released=rms(9600);
        require(released<note*.01,"MIDI note-off releases audio");
        std::set<unsigned> expected;
        if(argc==4) for(unsigned i=0;i<allocation.allocated;++i) expected.insert(48+i);
        else expected={60,64,67,72};
        for(auto key:expected) hw.sendMidi(0x90,uint8_t(key),100);
        rms(9600);const auto chord=rms(9600);
        std::set<unsigned> held,activeDsps;
        for(const auto& v:hw.voices().voice) if(v.state) {held.insert(v.note);activeDsps.insert(v.dsp);require(v.velocity==100,"MIDI velocity reaches native voice state");}
        require(held==expected,"MCU holds all independent chord notes");
        if(argc==4) require(activeDsps.size()==4,"Active MIDI voices span all four DSPs");
        hw.sendMidi(0xb0,123,0);rms(96000);const auto panic=rms(9600);
        std::cout<<"released="<<released<<" chord="<<chord<<" all_notes_off="<<panic<<'\n';
        require(chord>note*1.4,"Polyphonic chord has multiple voices");
        require(panic<note*.01,"All notes off releases voices");
        hw.sendMidi(0x90,60,100);rms(9600);
        hw.setParameter(1,4,0,0);rms(96000);const auto muted=rms(9600);std::cout<<"parameter_muted="<<muted<<std::endl;
        require(muted<note*.01,"Worker-safe parameter change mutes native output");
        hw.setParameter(1,4,0,127);rms(96000);require(rms(9600)>note*.9,"Native parameter change restores output");
        hw.sendMidi(0xb0,12,0);rms(96000);const auto ccMuted=rms(9600);std::cout<<"cc_muted="<<ccMuted<<std::endl;
        require(ccMuted<note*.01,"Mapped MIDI controller changes native output");
        hw.sendMidi(0xb0,12,127);rms(96000);const auto ccRestored=rms(9600);std::cout<<"cc_restored="<<ccRestored<<std::endl;
        require(ccRestored>note*.9,"Mapped MIDI controller restores native output");
        hw.sendMidi(0x90,60,0);rms(96000);require(rms(9600)<note*.01,"Zero-velocity note-on releases voice");
        hw.sendMidi(0x90,60,42);rms(9600);
        bool velocity=false;for(const auto& v:hw.voices().voice) if(v.state&&v.note==60) velocity=v.velocity==42;
        require(velocity,"Changed velocity reaches MCU voice state");
        std::cout<<"PASS Rack native patch/MIDI audio\n";return 0;
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
