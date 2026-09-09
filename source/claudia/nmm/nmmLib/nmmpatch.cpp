#include "nmmpatch.h"
#include "nmmpatchv2.h"
#include <iterator>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace nmm
{
	Patch Patch::load(const std::string& filename)
	{
		std::ifstream input(filename);
		if(!input) throw std::runtime_error("Cannot open patch: " + filename);
		auto patch=parse(input);
		const auto slash=filename.find_last_of("/\\");
		patch.name=filename.substr(slash==std::string::npos?0:slash+1);
		const auto dot=patch.name.find_last_of('.');if(dot!=std::string::npos) patch.name.resize(dot);
		return patch;
	}
	Patch Patch::parse(std::istream& source)
    {
        const std::string text{std::istreambuf_iterator<char>(source),{}};
        if(text.size()>1024*1024) throw std::runtime_error("Patch exceeds size limit");
        std::istringstream input(text.find("Version=Nord Modular patch 2.10")!=std::string::npos?convertPatchV2(text):text);
		Patch patch;
		std::string line, section;
		int area = -1;
		bool version = false;
		using Endpoint = std::tuple<int,int,int,int>; // area, module, port, output
		std::map<Endpoint, std::vector<Endpoint>> nets;
		std::map<Endpoint, int> colors;
		std::map<std::pair<int,int>,std::string> names;
		while(std::getline(input,line))
		{
			if(!line.empty() && line.back()=='\r') line.pop_back();
			if(line.empty()) continue;
			if(line.front()=='[') { section=line; area=-1; continue; }
			if(line.rfind("Version=",0)==0)
			{
				if(line!="Version=Nord Modular patch 3.0") throw std::runtime_error("Only Nord Modular patch 3.0 is currently supported");
				version=true; continue;
			}
			if(section=="[Header]")
			{
				std::istringstream row(line);
				std::vector<int> v;
				for(int n;row>>n;) v.push_back(n);
				if(!row.eof() || v.size()<4) throw std::runtime_error("Invalid patch header");
				for(unsigned i=0;i<4;++i) if(v[i]<0 || v[i]>127) throw std::runtime_error("Patch header range outside supported range");
				patch.keyRangeMin=uint8_t(v[0]); patch.keyRangeMax=uint8_t(v[1]);
				patch.velocityRangeMin=uint8_t(v[2]); patch.velocityRangeMax=uint8_t(v[3]);
				if(v.size()>=8)
				{
					if(v[7]<1 || v[7]>32) throw std::runtime_error("Invalid requested voice count");
					patch.requestedVoices=uint8_t(v[7]);
				}
				if(v.size()>=5)
				{
					if(v[4]<0 || v[4]>24) throw std::runtime_error("Invalid bend range");
					patch.bendRange=uint8_t(v[4]);
				}
				if(v.size()>=10)
                {
                    if(v[5]<0 || v[5]>127 || v[6]<0 || v[6]>1 || v[8]<0 || v[8]>65535 || v[9]<0 || v[9]>4) throw std::runtime_error("Invalid portamento, layout or octave header");
                    patch.portamentoTime=v[5];patch.portamento=v[6];patch.areaSeparator=v[8];patch.octaveShift=v[9];
                }
                if(v.size()>=12)
				{
					if(v[10]<0 || v[10]>1 || v[11]<0 || v[11]>1) throw std::runtime_error("Invalid voice retrigger mode");
					patch.voiceRetrigger=uint8_t(v[10]); patch.commonRetrigger=uint8_t(v[11]);
				}
				if(patch.keyRangeMin>patch.keyRangeMax || patch.velocityRangeMin>patch.velocityRangeMax)
					throw std::runtime_error("Invalid patch key or velocity range");
				continue;
			}
			if(section=="[NameDump]")
			{
				std::istringstream row(line);int module;
				if(area<0) {if(!(row>>area) || area<0 || area>1) throw std::runtime_error("Invalid module name area");}
				if(row>>module) {std::string name;std::getline(row>>std::ws,name);names[{area,module}]=name;}
				continue;
			}
			if(section=="[KnobMapDump]" || section=="[CtrlMapDump]")
			{
				std::istringstream row(line);
				int fileArea,module,param,knob; std::string extra;
				if(!(row>>fileArea>>module>>param>>knob) || (row>>extra) || fileArea<0 || fileArea>2 || module<1 || module>127 || param<0 || param>127 || knob<0 || knob>127)
					throw std::runtime_error("Invalid knob mapping");
				if(fileArea==2 && (module!=1 || param>3)) throw std::runtime_error("Invalid morph knob mapping");
				(section=="[KnobMapDump]"?patch.knobs:patch.controllers).push_back({uint16_t(fileArea),uint16_t(module),uint16_t(param),uint16_t(knob)});
				continue;
			}
			if(section=="[MorphMapDump]" || section=="[KeyboardAssignment]")
			{
				std::istringstream row(line);
				std::vector<int> v;
				for(int n;row>>n;) v.push_back(n);
				if(!row.eof() || v.empty()) throw std::runtime_error("Invalid morph record");
				if(section=="[KeyboardAssignment]" || area<0)
				{
					const bool keyboard=section=="[KeyboardAssignment]";
					if(v.size()!=4) throw std::runtime_error("Expected four morph settings");
					for(unsigned i=0;i<4;++i)
					{
						if(v[i]<0 || v[i]>(keyboard?2:127)) throw std::runtime_error("Invalid morph setting");
						(keyboard?patch.morphKeyboard:patch.morphValues)[i]=uint8_t(v[i]);
					}
					area=0;
				}
				else
				{
					if(v.size()%5) throw std::runtime_error("Invalid morph mapping");
					for(size_t i=0;i<v.size();i+=5)
					{
						if(v[i]<0 || v[i]>1 || v[i+1]<1 || v[i+1]>127 || v[i+2]<0 || v[i+2]>127 || v[i+3]<0 || v[i+3]>3 || v[i+4]<-127 || v[i+4]>127)
							throw std::runtime_error("Morph mapping outside supported range");
						patch.morphs.push_back({uint16_t(v[i]),uint16_t(v[i+1]),uint16_t(v[i+2]),uint16_t(v[i+3]),int16_t(v[i+4])});
					}
				}
				continue;
			}
			if(section!="[ModuleDump]" && section!="[ParameterDump]" && section!="[CableDump]" && section!="[CustomDump]") continue;
			std::istringstream row(line);
			std::vector<int> v;
			for(int n;row>>n;) v.push_back(n);
			if(!row.eof() || v.empty()) throw std::runtime_error("Invalid numeric patch record");
			if(area<0)
			{
				if(v[0]<0 || v[0]>1) throw std::runtime_error("Invalid patch area");
				area=v[0]; // Both file and firmware use common=0, polyphonic=1.
				v.erase(v.begin()); // Earlier v3 editors put the first record on this line.
				if(v.empty()) continue;
			}
			for(auto n:v) if(n<0 || n>255) throw std::runtime_error("Patch field outside supported range");
			if(section=="[ModuleDump]")
			{
				if(v.size()!=4 || !v[0] || v[0]>127 || !v[1] || v[1]>127) throw std::runtime_error("Invalid module record");
				patch.modules.push_back({uint16_t(area),uint16_t(v[0]),uint16_t(v[1]),uint8_t(v[2]),uint8_t(v[3])});
			}
			else if(section=="[CustomDump]")
            {
                if(v.size()<2 || v[0]<1 || v[0]>127 || v.size()!=size_t(v[1]+2)) throw std::runtime_error("Invalid custom module data");
                Patch::Custom custom{uint16_t(area),uint16_t(v[0]),{}};
                custom.data.assign(v.begin()+2,v.end());patch.custom.push_back(std::move(custom));
            }
            else if(section=="[ParameterDump]")
			{
				if(v.size()<3 || v.size()!=size_t(v[2]+3)) throw std::runtime_error("Invalid parameter record");
				for(int i=0;i<v[2];++i) patch.parameters.push_back({uint16_t(area),uint16_t(v[0]),uint16_t(i),uint16_t(v[i+3])});
			}
			else
			{
				if(v.size()!=7 || v[2]>63 || v[5]>63 || v[3]>1 || v[6]>1 || v[0]>6) throw std::runtime_error("Invalid cable record");
				const Endpoint a{area,v[1],v[2],v[3]}, b{area,v[4],v[5],v[6]};
				nets[a].push_back(b); nets[b].push_back(a); colors[a]=colors[b]=v[0];
			}
		}
		if(!version || patch.modules.empty()) throw std::runtime_error("Patch has no supported module data");
		for(auto& module:patch.modules) module.name=names[{module.area,module.index}];
		std::set<std::pair<int,int>> modules;
		for(const auto& m:patch.modules) if(!modules.emplace(m.area,m.index).second) throw std::runtime_error("Duplicate module index");
		for(const auto& p:patch.parameters) if(!modules.count({p.area,p.module})) throw std::runtime_error("Parameter references missing module");
		std::set<std::pair<unsigned,unsigned>> customTargets;
        for(const auto& c:patch.custom) if(!modules.count({c.area,c.module}) || !customTargets.emplace(c.area,c.module).second) throw std::runtime_error("Missing or duplicate custom module");
        std::set<unsigned> controllers;
        for(const auto& c:patch.controllers)
        {
            bool found=c.area==2;
            for(const auto& p:patch.parameters) found |= p.area==c.area && p.module==c.module && p.index==c.parameter;
            if(!found || !controllers.insert(c.index).second) throw std::runtime_error("Missing or duplicate MIDI controller target");
        }
        std::set<std::tuple<uint16_t,uint16_t,uint16_t>> morphTargets;
		if(patch.morphs.size()>25) throw std::runtime_error("Firmware supports at most 25 morph mappings");
		for(const auto& m:patch.morphs)
		{
			bool found=false;
			for(const auto& p:patch.parameters) if(p.area==m.area && p.module==m.module && p.index==m.parameter) found=true;
			if(!found || !morphTargets.emplace(m.area,m.module,m.parameter).second) throw std::runtime_error("Missing or duplicate morph target");
		}
		for(const auto& knob:patch.knobs)
		{
			if(knob.area==2) continue;
			if(!modules.count({knob.area,knob.module})) throw std::runtime_error("Knob references missing module");
			bool found=false;
			for(const auto& param:patch.parameters)
				if(param.area==knob.area && param.module==knob.module && param.index==knob.parameter) found=true;
			if(!found) throw std::runtime_error("Knob references missing parameter");
		}
		std::set<Endpoint> visited;
		for(const auto& entry:nets)
		{
			if(visited.count(entry.first)) continue;
			std::vector<Endpoint> pending{entry.first}, inputs, outputs;
			while(!pending.empty())
			{
				auto e=pending.back(); pending.pop_back();
				if(!visited.insert(e).second) continue;
				if(!modules.count({std::get<0>(e),std::get<1>(e)})) throw std::runtime_error("Cable references missing module");
				(std::get<3>(e)?outputs:inputs).push_back(e);
				for(auto neighbor:nets.at(e)) pending.push_back(neighbor);
			}
			if(outputs.size()!=1) throw std::runtime_error("Cable net must have exactly one output");
			const auto source=outputs.front();
			for(auto dest:inputs) patch.cables.push_back({uint16_t(std::get<0>(dest)),{uint8_t(std::get<1>(dest)),uint8_t(std::get<2>(dest)),uint8_t(std::get<1>(source)),uint8_t(std::get<2>(source)|0x40),uint8_t(colors[dest]),0}});
		}
		return patch;
	}
}
