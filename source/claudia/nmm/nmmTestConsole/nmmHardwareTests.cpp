#include "nmmLib/nmmhardware.h"
#include "nmmLib/nmmpatch.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <fstream>
#include <stdexcept>

namespace
{
	using Audio=std::vector<std::array<float,2>>;
	void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
	double rms(const Audio& audio,unsigned ch=0)
	{
		double sum=0,squares=0;
		for(auto f:audio) {require(std::isfinite(f[ch]),"Non-finite audio");sum+=f[ch];squares+=double(f[ch])*f[ch];}
		return std::sqrt(std::max(0.0,squares/audio.size()-std::pow(sum/audio.size(),2)));
	}
	double amplitude(const Audio& audio,double frequency)
	{
		double re=0,im=0;
		for(size_t i=0;i<audio.size();++i)
		{
			const auto w=.5-.5*std::cos(2*3.141592653589793*i/(audio.size()-1));
			const auto phase=2*3.141592653589793*frequency*i/96000;
			re+=w*audio[i][0]*std::cos(phase);im+=w*audio[i][0]*std::sin(phase);
		}
		return 4*std::hypot(re,im)/audio.size();
	}
}
int main(int argc,char** argv)
{
	try
	{
		require(argc==3 || argc==4,"Usage: nmmHardwareTests firmware.bin FourVoices.pch [diagnostic-prefix|--audio-driven]");
		nmm::Hardware hw(argv[1]);
        const bool audioDriven=argc==4 && std::string(argv[3])=="--audio-driven";
        if(audioDriven) {hw.setAudioDrivenExecution(64);hw.setDeadlineLinkedJit(true);}
		auto fixture=nmm::Patch::load(argv[2]);fixture.controllers.push_back({1,4,0,12});
        hw.boot(30000000);hw.loadPatch(fixture,30000000);
		constexpr uint32_t common=0x175f40+0x467a,poly=0x175f40+0x4c66;
		auto byte=[&](uint32_t a){return hw.readMemory('C',a);};
		auto word=[&](uint32_t a){return (byte(a)<<8)|byte(a+1);};
		require(byte(poly+0x1fc)==4 && byte(common+0x1fc)==1,"Four polyphonic voices and one shared area");
		std::set<uint32_t> programs;
		for(unsigned i=0;i<4;++i) programs.insert(word(poly+0x228+14*i+6));
		require(programs.size()==4,"Independent DSP sample programs");
		auto render=[&](unsigned frames){return hw.render(frames,10000000);};
		auto heldNotes=[&]()
		{
			std::set<uint32_t> notes;
			for(unsigned i=0;i<4;++i) if(byte(poly+0x3e8+14*i+12)) notes.insert(byte(poly+0x3e8+14*i+9));
			return notes;
		};
		hw.setMasterVolume(127);render(96000);
		hw.sendMidi(0x90,60,100);render(12000);
		const auto single=render(96000);
		const double level=rms(single),fundamental=amplitude(single,261.625565);
		std::cout<<"single_rms="<<level<<" sine_amplitude="<<fundamental<<'\n';
		require(level>.03 && level<.3 && fundamental>level,"Sine amplitude/tuning without makeup gain");
		require(rms(single,1)>.99*level && rms(single,1)<1.01*level,"Stereo routing");
		hw.sendMidi(0xe0,127,127);render(12000);
		require(amplitude(render(48000),293.664768)>fundamental*.9,"Native two-semitone pitch bend");
		hw.sendMidi(0xe0,0,64);render(12000);
		for(auto note:{64,67,71}) hw.sendMidi(0x90,uint8_t(note),100);
		render(12000);
		require(heldNotes()==std::set<uint32_t>({60,64,67,71}),"MCU allocated all chord notes");
		const auto chord=render(96000);
		for(auto frequency:{261.625565,329.627557,391.995436,493.883301})
		{
			const auto a=amplitude(chord,frequency);
			std::cout<<"chord_frequency="<<frequency<<" amplitude="<<a<<'\n';
			require(a>fundamental*.85 && a<fundamental*1.15,"Every voice contributes its own pitch and level");
		}
		hw.sendMidi(0x80,64,0);render(48000);
		require(heldNotes()==std::set<uint32_t>({60,67,71}),"Independent note release");
		const auto three=render(48000);
		require(amplitude(three,329.627557)<fundamental*.01,"Released voice no longer audible");
		hw.sendMidi(0xb0,64,127);render(1000);
		for(auto note:{60,67,71}) hw.sendMidi(0x80,uint8_t(note),0);
		render(12000);
		require(heldNotes().size()==3 && rms(render(12000))>level,"Sustain retains released voices");
		hw.sendMidi(0xb0,64,0);render(48000);
		require(heldNotes().empty() && rms(render(12000))<1e-5,"Pedal-up releases every voice");
		for(auto note:{60,64,67,71,74}) {hw.sendMidi(0x90,uint8_t(note),100);render(1000);}
		const auto stolen=heldNotes();
		require(stolen.size()==4 && stolen.count(74),"Fifth note steals a native voice");
		hw.sendMidi(0xb0,123,0);render(48000);
		require(heldNotes().empty() && rms(render(12000))<1e-5,"All notes off clears voices");
        // Repeated simultaneous chords with short release gaps must retain all
        // four MCU assignments and all four independent DSP fundamentals.
        hw.setPatchParameter(1,3,4,0);render(12000);
        for(unsigned window : {0u,1u})
        {
            if(!audioDriven) hw.setDspExecutionWindow(window);
            for(unsigned iteration=0;iteration<64;++iteration)
            {
                const unsigned transpose=iteration%3?0:iteration%5;
                for(auto note:{60,64,67,71}) hw.sendMidi(0x90,uint8_t(note+transpose),100);
                render(960);
                std::set<uint32_t> expectedNotes;
                for(auto note:{60,64,67,71}) expectedNotes.insert(note+transpose);
                if(heldNotes()!=expectedNotes) throw std::runtime_error("Retrigger MCU note loss: window="+std::to_string(window)+" iteration="+std::to_string(iteration));
                const auto audio=render(9600);
                for(auto note:expectedNotes)
                {
                    const double frequency=440*std::pow(2.0,(int(note)-69)/12.0);
                    if(amplitude(audio,frequency)<fundamental*.7) throw std::runtime_error("Retrigger DSP voice missing: window="+std::to_string(window)+" iteration="+std::to_string(iteration)+" note="+std::to_string(note));
                }
                for(auto note:expectedNotes) hw.sendMidi(iteration%2?0x90:0x80,uint8_t(note),0);
                render(iteration%9*37);
            }
            render(12000);
        }
        std::cout<<"PASS repeated short-gap four-note chords in reference and burst schedulers\n";
		hw.sendMidi(0x90,60,100);render(12000);
		hw.setMasterVolume(100);render(96000);
		const auto quiet=rms(render(48000));
		const auto expected=double(0xd297)/0x1feaa;
		require(std::abs(quiet/level-expected)<.01,"Native master-volume table curve");
		hw.sendMidi(0xb0,12,0);render(96000);
        require(rms(render(12000))<1e-5,"Imported MIDI controller mapping reaches the native output parameter");
        hw.sendMidi(0xb0,12,127);render(96000);
        require(rms(render(12000))>quiet*.9,"Native mapped CC restores output");
        hw.setPatchParameter(1,4,0,0);render(48000);
		require(rms(render(12000))<1e-5,"Live output knob actually reaches DSP");
		hw.setPatchParameter(1,4,0,127);render(48000);
		require(rms(render(12000))>quiet*.9,"Live output knob restores sound");
		hw.setMasterVolume(0);render(96000);
		require(rms(render(12000))<1e-5,"Master-volume zero is silent");
		hw.sendMidi(0x90,60,0);render(48000);
		require(heldNotes().empty(),"Velocity-zero note-on releases voice");
		// Route all four voices through one shared area and let a native morph
		// open its output level. This exercises the complete summing path.
		auto shared=nmm::Patch::load(argv[2]);
		for(auto& p:shared.parameters) if(p.area==1 && p.module==4 && p.index==1) p.value=2;
		shared.modules.push_back({0,1,127});shared.modules.push_back({0,2,4});
		shared.parameters.push_back({0,1,0,1}); // disable PolyAreaIn +6 dB
		shared.parameters.push_back({0,2,0,0});shared.parameters.push_back({0,2,1,0});shared.parameters.push_back({0,2,2,0});
		shared.cables.push_back({0,{2,0,1,0x40,0,0}});shared.cables.push_back({0,{2,1,1,0x41,0,0}});
		shared.morphs.push_back({0,2,0,0,127});shared.morphValues[0]=127;
		nmm::Hardware commonHw(argv[1]);
        if(audioDriven) {commonHw.setAudioDrivenExecution(64);commonHw.setDeadlineLinkedJit(true);}
        commonHw.boot(30000000);commonHw.loadPatch(shared,30000000);
		commonHw.setMasterVolume(127);commonHw.render(96000,10000000);
		commonHw.sendMidi(0x90,60,100);commonHw.render(12000,10000000);
		const auto commonSingle=amplitude(commonHw.render(96000,10000000),261.625565);
		std::cout<<"common_single_amplitude="<<commonSingle<<'\n';
		for(auto note:{64,67,71}) commonHw.sendMidi(0x90,uint8_t(note),100);
		commonHw.render(12000,10000000);
		const auto commonAudio=commonHw.render(96000,10000000);
		for(auto frequency:{261.625565,329.627557,391.995436,493.883301})
		{
			const auto a=amplitude(commonAudio,frequency);
			std::cout<<"common_frequency="<<frequency<<" amplitude="<<a<<'\n';
			require(a>commonSingle*.85 && a<commonSingle*1.15,"All voices reach the common output through native morph");
		}
		std::ofstream coefficientTrace;
        if(argc==4 && !audioDriven) {coefficientTrace.open(std::string(argv[3])+"-writes.csv");commonHw.enableRuntimeDiagnostics(&coefficientTrace);}
        commonHw.setPatchParameter(2,1,0,0);commonHw.render(48000,10000000);
		std::cout<<"morph_coefficient_at_half_second="<<commonHw.readMemory('X',0x96)<<'\n';
		if(argc==4 && !audioDriven)
		{
			std::ofstream state(std::string(argv[3])+"-muted.state");commonHw.dumpState(state);
			std::ofstream samples(std::string(argv[3])+"-muted.csv");
			for(unsigned i=0;i<1024;++i)
			{
				const auto frame=commonHw.render(1,1000000)[0];
				samples<<frame[0]<<','<<frame[1];
				for(unsigned a=0x90;a<0xa0;++a) samples<<','<<commonHw.readMemory('X',a)<<','<<commonHw.readMemory('Y',a);
				samples<<'\n';
			}
		}
		const auto muted=rms(commonHw.render(12000,10000000));
		std::cout<<"morph_mute_rms="<<muted<<'\n';
		require(commonHw.readMemory('X',0x96)==0 && muted<rms(commonAudio)*.001,
			"Common output morph clears its coefficient and attenuates every voice by at least 60 dB");
		commonHw.render(4096,10000000); // drain the serial/mix pipeline after the coefficient reaches zero
		const auto settledMute=rms(commonHw.render(12000,10000000));
		std::cout<<"morph_settled_mute_rms="<<settledMute<<'\n';
		require(settledMute<1e-8,"Settled zero coefficient produces constant DAC bias, without a noise gate");
		commonHw.setPatchParameter(2,1,0,127);commonHw.render(48000,10000000);
		require(rms(commonHw.render(12000,10000000))>level*.8,"Live morph restores shared output");
		std::cout<<"PASS MCU/DSP: four voices, pitch/bend, stereo, release, sustain, stealing, panic, native volume, live parameters, common area, morphs\n";
		return 0;
	}
	catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
