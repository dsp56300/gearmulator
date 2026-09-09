#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace nmm
{
    // Legacy editor-file translation, performed only during patch loading.
    // Field meanings checked against msg/g2ools nord/nm1/file.py and modules.py.
    inline std::string convertPatchV2(const std::string& text)
    {
        using Fields=std::map<std::string,std::string>;
        std::map<std::string,Fields> sections;
        std::string section,line;std::istringstream input(text);
        const auto trim=[](std::string s) {const auto a=s.find_first_not_of(" \t\r");return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);};
        while(std::getline(input,line))
        {
            line=trim(line);if(line.empty()) continue;
            if(line.front()=='[' && line.back()==']') {section=line.substr(1,line.size()-2);continue;}
            const auto equal=line.find('=');if(equal==std::string::npos) throw std::runtime_error("Invalid v2 patch record");
            auto key=trim(line.substr(0,equal));for(auto& c:key) c=char(std::tolower(static_cast<unsigned char>(c)));
            if(!sections[section].emplace(key,trim(line.substr(equal+1))).second) throw std::runtime_error("Duplicate v2 field");
        }
        const auto number=[](const Fields& f,const std::string& key,long fallback=0)
        {
            const auto it=f.find(key);if(it==f.end()) return fallback;
            size_t end=0;const auto value=std::stol(it->second,&end);
            if(end!=it->second.size()) throw std::runtime_error("Invalid v2 integer");return value;
        };
        const auto& header=sections["Header"];
        if(header.at("version")!="Nord Modular patch 2.10") throw std::runtime_error("Unsupported legacy patch version");
        std::map<unsigned,Fields> modules;unsigned morphModule=0;
        for(const auto& s:sections) if(s.first.rfind("Module ",0)==0)
        {
            size_t end=0;const auto index=std::stoul(s.first.substr(7),&end);
            if(end!=s.first.size()-7 || !index || index>127) throw std::runtime_error("Invalid v2 module index");
            if(number(s.second,"type")==6) {if(morphModule) throw std::runtime_error("Multiple legacy morph modules");morphModule=index;}
            modules[index]=s.second;
        }
        std::ostringstream out;
        out<<"[Header]\nVersion=Nord Modular patch 3.0\n"
           <<number(header,"kbrangemin")<<' '<<number(header,"kbrangemax",127)<<' '
           <<number(header,"velrangemin")<<' '<<number(header,"velrangemax",127)<<' '
           <<number(header,"bendrange",2)<<' '<<number(header,"pmtime")<<' '<<number(header,"pmmode")<<' '
           <<number(header,"voices",1)<<" 4000 "<<number(header,"octshift",2)<<' '<<number(header,"retrig",1)<<" 0\n[/Header]\n";
        out<<"[ModuleDump]\n1\n";
        for(const auto& m:modules) if(m.first!=morphModule)
            out<<m.first<<' '<<number(m.second,"type")<<' '<<number(m.second,"col")<<' '<<2*number(m.second,"row")<<'\n';
        out<<"[/ModuleDump]\n[NameDump]\n1\n";
        for(const auto& m:modules) if(m.first!=morphModule) {auto name=m.second.find("name");if(name!=m.second.end()) out<<m.first<<' '<<name->second<<'\n';}
        out<<"[/NameDump]\n[ParameterDump]\n1\n";
        const std::map<unsigned,unsigned> reversedMute{{9,5},{10,4},{11,3},{12,3},{13,3},{58,15},{85,4},{95,6},{96,3}};
        for(const auto& m:modules) if(m.first!=morphModule)
        {
            const auto type=number(m.second,"type");std::map<unsigned,long> params;
            for(const auto& field:m.second) if(field.first.size()>1 && field.first[0]=='p' && std::isdigit(static_cast<unsigned char>(field.first[1])))
            {
                size_t end=0;const auto index=std::stoul(field.first.substr(1),&end);
                if(end!=field.first.size()-1 || index>127) throw std::runtime_error("Invalid legacy parameter index");
                params[index]=std::clamp(number(m.second,field.first),0l,127l);
            }
            const auto mute=reversedMute.find(type);
            if(mute!=reversedMute.end()) params[mute->second]=1-std::clamp(params[mute->second],0l,1l);
            if(type==106) for(unsigned i=18;i<24;++i) params[i]=1-std::clamp(params[i],0l,1l);
            if(m.second.count("bp0"))
            {
                const auto bits=static_cast<uint32_t>(number(m.second,"bp0"));
                if(type==17) for(unsigned i=0;i<32;++i) params[4+i]=(bits>>i)&1;
                else if(type==15 || type==90 || type==91) params[type==91?18:20]=bits!=0;
                else throw std::runtime_error("Unsupported legacy packed parameters for module "+std::to_string(type));
            }
            if(params.empty()) continue;
            const auto count=params.rbegin()->first+1;out<<m.first<<' '<<type<<' '<<count;
            for(unsigned i=0;i<count;++i) out<<' '<<params[i];out<<'\n';
        }
        out<<"[/ParameterDump]\n[CableDump]\n1\n";
        for(const auto& m:modules) if(m.first!=morphModule)
            for(unsigned port=0;port<64;++port)
            {
                const auto key=std::to_string(port);if(!m.second.count("im"+key)) continue;
                const auto source=number(m.second,"im"+key),connection=number(m.second,"ih"+key),color=number(m.second,"ic"+key);
                if(source==long(morphModule)) throw std::runtime_error("Cable to legacy morph module");
                out<<color<<' '<<m.first<<' '<<port<<" 0 "<<source<<' '<<(connection&63)<<' '<<bool(connection&64)<<'\n';
            }
        out<<"[/CableDump]\n[MorphMapDump]\n";
        for(unsigned i=0;i<4;++i) out<<(morphModule?number(modules[morphModule],"p"+std::to_string(i)):0)<<' ';
        out<<'\n';const auto& morphs=sections["Morphs"];
        for(unsigned i=0;i<25;++i)
        {
            const auto key=std::to_string(i);if(!morphs.count("m"+key)) continue;
            out<<"1 "<<number(morphs,"m"+key)<<' '<<number(morphs,"p"+key)<<' '<<number(morphs,"g"+key)<<' '<<number(morphs,"d"+key)<<'\n';
        }
        out<<"[/MorphMapDump]\n";
        std::array<unsigned,4> keyboard{};
        for(const auto& mapping:std::initializer_list<std::pair<const char*,const char*>>{{"Controllers","CtrlMapDump"},{"Knobs","KnobMapDump"}})
        {
            out<<'['<<mapping.second<<"]\n";const auto& fields=sections[mapping.first];
            for(unsigned i=0;i<128;++i)
            {
                const auto key=std::to_string(i);if(!fields.count("m"+key)) continue;
                const auto module=number(fields,"m"+key),param=number(fields,"p"+key);
                if(module==long(morphModule))
                {
                    if(param<0 || param>3) throw std::runtime_error("Invalid legacy morph mapping");
                    if(std::string(mapping.first)=="Controllers" && i>=126) {keyboard[param]=i==126?1:2;continue;}
                    out<<"2 1 "<<param<<' '<<i<<'\n';
                }
                else out<<"1 "<<module<<' '<<param<<' '<<i<<'\n';
            }
            out<<"[/"<<mapping.second<<"]\n";
        }
        out<<"[KeyboardAssignment]\n";for(auto value:keyboard) out<<value<<' ';out<<"\n[/KeyboardAssignment]\n";
        return out.str();
    }
}
