#include "nmmLib/nmmpatch.h"
#include "nmmLib/nmmrom.h"
#include "nmmLib/nmmcodec.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <limits>

static void check(bool condition,const char* message)
{
	if(!condition) throw std::runtime_error(message);
}
int main(int argc,char** argv)
{
	try
	{
		check(nmm::sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty vector");
		check(nmm::sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc vector");
		check(argc==2,"Expected patch directory");
		const auto p=nmm::Patch::load(std::string(argv[1])+"/101.pch");
		check(p.modules.size()==5 && p.parameters.size()==26 && p.cables.size()==6,"101 patch counts");
		check(p.keyRangeMin==0 && p.keyRangeMax==127 && p.velocityRangeMin==0 && p.velocityRangeMax==127,"101 header ranges");
		check(p.knobs.size()==5 && p.knobs[1].area==1 && p.knobs[1].module==3 && p.knobs[1].parameter==0 && p.knobs[1].index==0,"101 knob assignment conversion");
		unsigned outputs=0;
		for(auto c:p.cables)
		{
			check(c.area==1 && (c.record[3]&0x40),"Firmware output port encoding");
			if(c.record[0]==5) {++outputs;check(c.record[2]==4 && c.record[3]==0x41,"Shared input net resolution");}
		}
		check(outputs==2,"Stereo output connections");
		check(nmm::Patch::load(std::string(argv[1])+"/FourVoices.pch").requestedVoices==4,"Requested polyphony");
		std::istringstream inlineArea("[Header]\nVersion=Nord Modular patch 3.0\n0 127 0 127 2 0 0 4\n[ModuleDump]\n1 1 7 0 0\n[ParameterDump]\n1 1 7 2 64 64\n");
		const auto older=nmm::Patch::parse(inlineArea);
		check(older.modules[0].area==1 && older.parameters.size()==2 && older.requestedVoices==4,"Earlier v3 inline area record");
		check(nmm::encodeAdc(-1)==0x800000 && nmm::encodeAdc(1)==0x7fff00 && nmm::encodeAdc(.5)==0x400000,"ADC signed 16-bit quantization and clipping");
        check(nmm::encodeAdc(std::numeric_limits<float>::quiet_NaN())==0 && nmm::encodeAdc(std::numeric_limits<float>::infinity())==0,"ADC rejects non-finite host input with fast-math enabled");
        check(nmm::decodeDac(0)==0 && nmm::decodeDac(0x20000)==-1 && nmm::decodeDac(0x3ffff)==-1.0f/131072 && nmm::decodeDac(0xffffff)==-1.0f/131072,"18-bit DAC sign/word decoding");
		check(nmm::decodeDac(0x1ffff)==1.0f-1.0f/131072,"18-bit positive full scale");
		const auto simple=nmm::Patch::load(std::string(argv[1])+"/SimpleSynth02.pch");
		check(simple.modules.size()==16 && simple.keyRangeMax==127 && simple.velocityRangeMax==127,"SimpleSynth02 patch");
		check(simple.morphs.size()==8 && simple.morphValues[1]==127 && simple.morphs.back().range==-127,"Saved morph positions and signed ranges");
		const auto square=nmm::Patch::load(std::string(argv[1])+"/SimpleSqr1.pch");
		check(square.knobs.size()==3 && square.knobs[2].index==2,"Micro panel knob mappings");
		check(square.modules.size()==5 && square.keyRangeMax==127 && square.velocityRangeMax==127,"SimpleSqr1 patch");
		const auto basic=nmm::Patch::load(std::string(argv[1])+"/BasicOsc.pch");
		check(basic.modules.size()==2 && basic.cables.size()==1,"Basic oscillator patch");
		check(nmm::Patch::load(std::string(argv[1])+"/Gong01.pch").modules.size()==27,"Gong module count");
        const auto drum=nmm::Patch::load(std::string(argv[1])+"/BDrm.pch");
        check(drum.modules.size()==27 && drum.morphs.size()==9 && drum.knobs.size()==7,"Legacy drum graph and mappings");
        check(drum.controllers.size()==1 && drum.controllers[0].index==1 && drum.morphKeyboard[0]==1,"Legacy MIDI/morph assignments");
        bool mute=false;for(auto param:drum.parameters) if(param.module==5 && param.index==4) mute=param.value==0;
        check(mute,"Legacy oscillator mute polarity");
        check(!simple.custom.empty() && !square.custom.empty(),"Custom module data retained");
        std::istringstream fields("[Header]\nVersion=Nord Modular patch 3.0\n0 127 0 127 12 45 1 4 1234 3 1 0\n[ModuleDump]\n1 1 7 0 0\n[ParameterDump]\n1 1 7 2 64 64\n[CtrlMapDump]\n1 1 0 12\n");
        auto configured=nmm::Patch::parse(fields);
        check(configured.portamento==1 && configured.portamentoTime==45 && configured.octaveShift==3 && configured.areaSeparator==1234 && configured.controllers.size()==1,"Remaining v3 header/controller fields");
        for(const auto suffix:{"[CustomDump]\n1\n2 1 0\n","[CtrlMapDump]\n1 99 0 12\n"})
        {
            std::istringstream invalid("[Header]\nVersion=Nord Modular patch 3.0\n0 127 0 127\n[ModuleDump]\n1 1 7 0 0\n"+std::string(suffix));
            bool rejected=false;try {nmm::Patch::parse(invalid);} catch(const std::runtime_error&) {rejected=true;}
            check(rejected,"Reject dangling extended patch records");
        }
		std::cout << "NMM ROM hash and patch parser checks passed\n";
		return 0;
	}
	catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
